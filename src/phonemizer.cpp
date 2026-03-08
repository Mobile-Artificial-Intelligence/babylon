#include "babylon.h"
#include <onnxruntime_cxx_api.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <iostream>

// ---------------------------------------------------------------------------
// Input character vocabulary (text → model input IDs)
// Matches OpenPhonemizerInputTokenizer.kt textSymbols
// ---------------------------------------------------------------------------

static const std::unordered_map<std::string, int> TEXT_SYMBOLS = {
    {"_", 0}, {"<en_us>", 1}, {"<end>", 2},
    {"a", 3},  {"b", 4},  {"c", 5},  {"d", 6},  {"e", 7},  {"f", 8},
    {"g", 9},  {"h", 10}, {"i", 11}, {"j", 12}, {"k", 13}, {"l", 14},
    {"m", 15}, {"n", 16}, {"o", 17}, {"p", 18}, {"q", 19}, {"r", 20},
    {"s", 21}, {"t", 22}, {"u", 23}, {"v", 24}, {"w", 25}, {"x", 26},
    {"y", 27}, {"z", 28},
    {"A", 29}, {"B", 30}, {"C", 31}, {"D", 32}, {"E", 33}, {"F", 34},
    {"G", 35}, {"H", 36}, {"I", 37}, {"J", 38}, {"K", 39}, {"L", 40},
    {"M", 41}, {"N", 42}, {"O", 43}, {"P", 44}, {"Q", 45}, {"R", 46},
    {"S", 47}, {"T", 48}, {"U", 49}, {"V", 50}, {"W", 51}, {"X", 52},
    {"Y", 53}, {"Z", 54},
};

// ---------------------------------------------------------------------------
// Output phoneme vocabulary (model output IDs → IPA strings)
// Matches OpenPhonemizerInputTokenizer.kt phonemeSymbols
// ---------------------------------------------------------------------------

static const std::unordered_map<int, std::string> PHONEME_SYMBOLS = {
    {0,  "_"},    {1,  "<en_us>"}, {2,  "<end>"},
    {3,  "a"},    {4,  "b"},  {5,  "d"},  {6,  "e"},
    {7,  "f"},    {8,  "g"},  {9,  "h"},  {10, "i"},
    {11, "j"},    {12, "k"},  {13, "l"},  {14, "m"},
    {15, "n"},    {16, "o"},  {17, "p"},  {18, "r"},
    {19, "s"},    {20, "t"},  {21, "u"},  {22, "v"},
    {23, "w"},    {24, "x"},  {25, "y"},  {26, "z"},
    {27, "æ"},    {28, "ç"},  {29, "ð"},  {30, "ø"},
    {31, "ŋ"},    {32, "œ"},  {33, "ɐ"},  {34, "ɑ"},
    {35, "ɔ"},    {36, "ə"},  {37, "ɛ"},  {38, "ɜ"},
    {39, "ɝ"},    {40, "ɹ"},  {41, "ɚ"},  {42, "ɡ"},
    {43, "ɪ"},    {44, "ʁ"},  {45, "ʃ"},  {46, "ʊ"},
    {47, "ʌ"},    {48, "ʏ"},  {49, "ʒ"},  {50, "ʔ"},
    {51, "ˈ"},    {52, "ˌ"},  {53, "ː"},  {54, "\u0303"},
    {55, "\u030D"},{56, "\u0325"},{57, "\u0329"},{58, "\u032F"},
    {59, "\u0361"},{60, "θ"},  {61, "'"},  {62, "ɾ"},
    {63, "ᵻ"},
};

static const int BLANK_ID = 0;
static const int END_ID   = 2;
static const int SEQ_LEN  = 64;
static const int CHAR_REPEATS = 3;

// ---------------------------------------------------------------------------
// Punctuation sets for phonemize output formatting
// ---------------------------------------------------------------------------

static const std::unordered_map<char, int> PUNCT_BEFORE = {
    {'.', 1}, {',', 1}, {'!', 1}, {'?', 1}, {';', 1}, {':', 1}, {')', 1}, {']', 1}, {'}', 1},
};

static const std::unordered_map<char, int> PUNCT_AFTER = {
    {'(', 1}, {'[', 1}, {'{', 1},
};

// ---------------------------------------------------------------------------
// Simple streaming dictionary JSON parser
// Handles {"en_us": {"word": "phonemes", ...}} format
// ---------------------------------------------------------------------------

static std::string read_json_string(const char* buf, size_t len, size_t& pos) {
    // pos should be at the opening '"'
    ++pos; // skip opening "
    std::string result;
    result.reserve(32);
    while (pos < len) {
        char c = buf[pos++];
        if (c == '"') break;
        if (c == '\\' && pos < len) {
            char e = buf[pos++];
            if (e == 'u' && pos + 4 <= len) {
                // Parse \uXXXX → UTF-8
                char hex[5] = {buf[pos], buf[pos+1], buf[pos+2], buf[pos+3], '\0'};
                pos += 4;
                uint32_t cp = (uint32_t)std::strtoul(hex, nullptr, 16);
                if (cp < 0x80) {
                    result += (char)cp;
                } else if (cp < 0x800) {
                    result += (char)(0xC0 | (cp >> 6));
                    result += (char)(0x80 | (cp & 0x3F));
                } else {
                    result += (char)(0xE0 | (cp >> 12));
                    result += (char)(0x80 | ((cp >> 6) & 0x3F));
                    result += (char)(0x80 | (cp & 0x3F));
                }
            } else if (e == 'n') result += '\n';
            else if (e == 't') result += '\t';
            else if (e == 'r') result += '\r';
            else result += e;
        } else {
            result += c;
        }
    }
    return result;
}

static std::unordered_map<std::string, std::string> load_dictionary(const std::string& path) {
    std::unordered_map<std::string, std::string> dict;
    if (path.empty()) return dict;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[OpenPhonemizer] Could not open dictionary: " << path << std::endl;
        return dict;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    const char* buf = content.c_str();
    size_t len = content.size();

    // Find "en_us" key
    std::string target = "\"en_us\"";
    size_t en_us_pos = content.find(target);
    if (en_us_pos == std::string::npos) return dict;

    size_t pos = en_us_pos + target.size();

    // Skip to opening '{' of the en_us object
    while (pos < len && buf[pos] != '{') ++pos;
    if (pos >= len) return dict;
    ++pos; // skip '{'

    dict.reserve(130000);

    while (pos < len) {
        // Skip whitespace and commas
        while (pos < len && (buf[pos] == ' ' || buf[pos] == '\n' ||
                              buf[pos] == '\r' || buf[pos] == '\t' ||
                              buf[pos] == ',')) {
            ++pos;
        }
        if (pos >= len || buf[pos] == '}') break;

        if (buf[pos] == '"') {
            std::string key = read_json_string(buf, len, pos);
            // Skip whitespace and ':'
            while (pos < len && buf[pos] != '"') ++pos;
            std::string value = read_json_string(buf, len, pos);
            dict[key] = value;
        } else {
            ++pos;
        }
    }

    return dict;
}

// ---------------------------------------------------------------------------
// Word tokenizer: splits text into word and punctuation tokens
// Matches WORD_REGEX = [\w']+|[^\w\s]
// ---------------------------------------------------------------------------

static std::vector<std::string> word_tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = (unsigned char)text[i];
        if (std::isspace(c)) {
            ++i;
        } else if (std::isalnum(c) || c == '_' || c == '\'') {
            size_t start = i;
            while (i < text.size()) {
                unsigned char ch = (unsigned char)text[i];
                if (std::isalnum(ch) || ch == '_' || ch == '\'') ++i;
                else break;
            }
            tokens.push_back(text.substr(start, i - start));
        } else {
            tokens.push_back(std::string(1, (char)c));
            ++i;
        }
    }
    return tokens;
}

static bool is_punct_token(const std::string& tok) {
    if (tok.empty()) return false;
    for (char c : tok) {
        if (std::isalnum((unsigned char)c) || c == '_' || c == '\'') return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// OpenPhonemizer namespace implementation
// ---------------------------------------------------------------------------

namespace OpenPhonemizer {

static const std::array<const char*, 1> INPUT_NAMES  = {"text"};
static const std::array<const char*, 1> OUTPUT_NAMES = {"output"};

std::vector<int64_t> encode_word(const std::string& word) {
    // Lowercase and replace spaces with '_'
    std::string cleaned;
    cleaned.reserve(word.size());
    for (char c : word) {
        cleaned += (char)std::tolower((unsigned char)c);
    }
    for (char& c : cleaned) {
        if (c == ' ') c = '_';
    }

    std::vector<int64_t> ids;
    ids.reserve(SEQ_LEN);

    // Start token
    ids.push_back(1); // <en_us>

    // Repeated chars
    for (char c : cleaned) {
        auto it = TEXT_SYMBOLS.find(std::string(1, c));
        if (it == TEXT_SYMBOLS.end()) continue;
        for (int r = 0; r < CHAR_REPEATS; ++r) {
            ids.push_back(it->second);
        }
    }

    // End token
    ids.push_back(2); // <end>

    // Pad to SEQ_LEN
    while ((int)ids.size() < SEQ_LEN) ids.push_back(0);
    if ((int)ids.size() > SEQ_LEN) ids.resize(SEQ_LEN);

    return ids;
}

std::string decode_phonemes(const float* logits, int seq_len, int vocab_size) {
    // 1. Argmax across vocab dim at each position
    std::vector<int> argmax(seq_len);
    for (int i = 0; i < seq_len; ++i) {
        const float* row = logits + i * vocab_size;
        int best = 0;
        for (int j = 1; j < vocab_size; ++j) {
            if (row[j] > row[best]) best = j;
        }
        argmax[i] = best;
    }

    // 2. CTC decode: collapse consecutive identical, remove blank, stop at END
    std::string result;
    int prev = -1;
    for (int id : argmax) {
        if (id == prev) continue;
        prev = id;
        if (id == BLANK_ID) continue;
        if (id == END_ID)   break;
        auto it = PHONEME_SYMBOLS.find(id);
        if (it == PHONEME_SYMBOLS.end()) continue;
        const std::string& sym = it->second;
        if (sym.front() == '<') continue; // skip special tokens
        result += sym;
    }
    return result;
}

Session::Session(
    const std::string& model_path,
    const std::string& dictionary_path,
    const bool use_punctuation
) : env(ORT_LOGGING_LEVEL_WARNING, "OpenPhonemizer"), session(nullptr),
    use_punctuation(use_punctuation)
{
    env.DisableTelemetryEvents();

    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(1);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    session = new Ort::Session(env, (const ORTCHAR_T*)model_path.c_str(), opts);

    dictionary = load_dictionary(dictionary_path);
}

Session::~Session() {
    delete session;
}

std::string Session::phonemize_word(const std::string& word) {
    std::vector<int64_t> input_ids = encode_word(word);
    std::vector<int64_t> shape = {1, SEQ_LEN};

    Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::vector<Ort::Value> inputs;
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(
        mem_info, input_ids.data(), input_ids.size(), shape.data(), shape.size()
    ));

    auto outputs = session->Run(
        Ort::RunOptions{nullptr},
        INPUT_NAMES.data(), inputs.data(), INPUT_NAMES.size(),
        OUTPUT_NAMES.data(), OUTPUT_NAMES.size()
    );

    if (outputs.empty()) return "";

    const float* logits = outputs.front().GetTensorData<float>();
    auto out_shape = outputs.front().GetTensorTypeAndShapeInfo().GetShape();

    // Expected shape: [1, 64, vocab_size]
    int seq_len   = (int)out_shape[1];
    int vocab_size = (int)out_shape[2];

    return decode_phonemes(logits, seq_len, vocab_size);
}

std::string Session::phonemize(const std::string& text) {
    std::string normalized = normalize_text(text);
    std::vector<std::string> tokens = word_tokenize(normalized);

    std::string sb;
    sb.reserve(normalized.size() * 2);

    for (const auto& token : tokens) {
        bool is_punct = is_punct_token(token);

        if (is_punct) {
            char first = token[0];
            if (PUNCT_BEFORE.count(first)) {
                // Remove trailing space before attaching
                while (!sb.empty() && sb.back() == ' ') sb.pop_back();
                sb += token;
            } else if (PUNCT_AFTER.count(first)) {
                sb += token;
            } else {
                // Other punctuation: add with leading space
                if (!sb.empty() && sb.back() != ' ') sb += ' ';
                sb += token;
            }
        } else {
            // Word
            bool last_is_punct_after = (!sb.empty() && PUNCT_AFTER.count(sb.back()));
            if (!sb.empty() && sb.back() != ' ' && !last_is_punct_after) {
                sb += ' ';
            }
            std::string word_lower = token;
            std::transform(word_lower.begin(), word_lower.end(), word_lower.begin(),
                           [](unsigned char c){ return std::tolower(c); });

            auto it = dictionary.find(word_lower);
            if (it != dictionary.end()) {
                sb += it->second;
            } else {
                sb += phonemize_word(word_lower);
            }
        }
    }

    // Trim trailing whitespace
    while (!sb.empty() && sb.back() == ' ') sb.pop_back();

    return sb;
}

std::vector<int64_t> Session::phonemize_tokens(const std::string& text) {
    return Kokoro::encode_phonemes(phonemize(text));
}

} // namespace OpenPhonemizer
