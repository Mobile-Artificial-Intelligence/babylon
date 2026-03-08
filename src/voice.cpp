#include "babylon.h"
#include <onnxruntime_cxx_api.h>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>
#include <stdexcept>

// ---------------------------------------------------------------------------
// WAV file helper
// ---------------------------------------------------------------------------

struct WavHeader {
    uint8_t  RIFF[4]         = {'R', 'I', 'F', 'F'};
    uint32_t chunk_size;
    uint8_t  WAVE[4]         = {'W', 'A', 'V', 'E'};
    uint8_t  fmt[4]          = {'f', 'm', 't', ' '};
    uint32_t fmt_size        = 16;
    uint16_t audio_format    = 1;    // PCM
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t bytes_per_second;
    uint16_t block_align;
    uint16_t bits_per_sample = 16;
    uint8_t  data[4]         = {'d', 'a', 't', 'a'};
    uint32_t data_size;
};

static void write_wav(
    const std::string& path,
    const int16_t* samples,
    size_t n_samples,
    uint32_t sample_rate,
    uint16_t channels = 1
) {
    std::ofstream f(path, std::ios::binary);
    uint16_t sample_width = 2;

    WavHeader hdr;
    hdr.num_channels     = channels;
    hdr.sample_rate      = sample_rate;
    hdr.bytes_per_second = sample_rate * sample_width * channels;
    hdr.block_align      = sample_width * channels;
    hdr.data_size        = (uint32_t)(n_samples * sample_width * channels);
    hdr.chunk_size       = hdr.data_size + sizeof(WavHeader) - 8;

    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    f.write(reinterpret_cast<const char*>(samples), n_samples * sizeof(int16_t));
}

// ---------------------------------------------------------------------------
// VITS namespace
// ---------------------------------------------------------------------------

namespace Vits {

static const std::array<const char*, 3> INPUT_NAMES  = {"input", "input_lengths", "scales"};
static const std::array<const char*, 1> OUTPUT_NAMES = {"output"};

static const float FMIN = static_cast<float>(std::numeric_limits<int16_t>::min());
static const float FMAX = static_cast<float>(std::numeric_limits<int16_t>::max());

SequenceTokenizer::SequenceTokenizer(
    const std::vector<std::string>& phonemes,
    const std::vector<int>& phoneme_ids
) {
    if (phonemes.size() != phoneme_ids.size()) {
        throw std::invalid_argument("Phonemes and phoneme IDs must have the same length.");
    }
    for (size_t i = 0; i < phonemes.size(); ++i) {
        token_to_idx[phonemes[i]] = phoneme_ids[i];
    }
}

std::vector<int64_t> SequenceTokenizer::operator()(const std::vector<std::string>& phonemes) const {
    std::vector<int64_t> ids = {1, 0};
    for (const auto& p : phonemes) {
        auto it = token_to_idx.find(p);
        if (it == token_to_idx.end()) {
            std::cerr << "[VITS] Unknown phoneme token: " << p << std::endl;
            continue;
        }
        ids.push_back(it->second);
        ids.push_back(0);
    }
    ids.push_back(2);
    return ids;
}

Session::Session(const std::string& model_path)
    : env(ORT_LOGGING_LEVEL_WARNING, "VITS"), session(nullptr), phoneme_tokenizer(nullptr)
{
    env.DisableTelemetryEvents();

    Ort::SessionOptions opts;
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    opts.DisableMemPattern();

    session = new Ort::Session(env, (const ORTCHAR_T*)model_path.c_str(), opts);

    Ort::ModelMetadata meta = session->GetModelMetadata();
    Ort::AllocatorWithDefaultOptions alloc;

    // Load phonemes
    std::string phoneme_str = meta.LookupCustomMetadataMapAllocated("phonemes", alloc).get();
    std::vector<std::string> phonemes;
    std::istringstream phoneme_ss(phoneme_str);
    std::string buf;
    while (phoneme_ss >> buf) {
        phonemes.push_back(buf == "<space>" ? " " : buf);
    }

    // Load phoneme IDs
    std::string id_str = meta.LookupCustomMetadataMapAllocated("phoneme_ids", alloc).get();
    std::vector<int> phoneme_ids;
    std::istringstream id_ss(id_str);
    while (id_ss >> buf) phoneme_ids.push_back(std::stoi(buf));

    sample_rate = std::stoi(meta.LookupCustomMetadataMapAllocated("sample_rate", alloc).get());

    float noise_scale  = std::stof(meta.LookupCustomMetadataMapAllocated("noise_scale",  alloc).get());
    float length_scale = std::stof(meta.LookupCustomMetadataMapAllocated("length_scale", alloc).get());
    float noise_w      = std::stof(meta.LookupCustomMetadataMapAllocated("noise_w",      alloc).get());
    scales = {noise_scale, length_scale, noise_w};

    phoneme_tokenizer = new SequenceTokenizer(phonemes, phoneme_ids);
}

Session::~Session() {
    delete session;
    delete phoneme_tokenizer;
}

void Session::tts(const std::vector<std::string>& phonemes, const std::string& output_path) {
    Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::vector<int64_t> ids   = phoneme_tokenizer->operator()(phonemes);
    std::vector<int64_t> shape = {1, (int64_t)ids.size()};

    std::vector<Ort::Value> inputs;

    inputs.push_back(Ort::Value::CreateTensor<int64_t>(
        mem_info, ids.data(), ids.size(), shape.data(), shape.size()
    ));

    std::vector<int64_t> len_val   = {(int64_t)ids.size()};
    std::vector<int64_t> len_shape = {1};
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(
        mem_info, len_val.data(), len_val.size(), len_shape.data(), len_shape.size()
    ));

    std::vector<int64_t> scales_shape = {(int64_t)scales.size()};
    inputs.push_back(Ort::Value::CreateTensor<float>(
        mem_info, scales.data(), scales.size(), scales_shape.data(), scales_shape.size()
    ));

    auto outputs = session->Run(
        Ort::RunOptions{nullptr},
        INPUT_NAMES.data(), inputs.data(), INPUT_NAMES.size(),
        OUTPUT_NAMES.data(), OUTPUT_NAMES.size()
    );

    if (outputs.empty()) throw std::runtime_error("[VITS] No output tensor.");

    const float* out = outputs.front().GetTensorData<float>();
    auto out_shape   = outputs.front().GetTensorTypeAndShapeInfo().GetShape();
    int64_t n        = out_shape.back();

    // Normalize to int16 range
    float peak = 0.01f;
    for (int64_t i = 0; i < n; ++i) {
        float v = std::abs(out[i]);
        if (v > peak) peak = v;
    }
    float scale = 32767.0f / peak;

    std::vector<int16_t> pcm(n);
    for (int64_t i = 0; i < n; ++i) {
        float v = out[i] * scale;
        if (v < FMIN) v = FMIN;
        if (v > FMAX) v = FMAX;
        pcm[i] = (int16_t)v;
    }

    write_wav(output_path, pcm.data(), pcm.size(), (uint32_t)sample_rate);
}

} // namespace Vits

// ---------------------------------------------------------------------------
// Kokoro namespace
// ---------------------------------------------------------------------------

namespace Kokoro {

// Output tokenizer vocab for Kokoro model
// Matches OpenPhonemizerOutputTokenizer.kt
static const std::unordered_map<std::string, int> KOKORO_VOCAB = {
    {"$",  0},  {";",  1},  {":",  2},  {",",  3},  {".",  4},
    {"!",  5},  {"?",  6},
    {"\xE2\x80\x94", 9},   // em dash —
    {"\xE2\x80\xA6", 10},  // ellipsis …
    {"\"", 11}, {"(",  12}, {")",  13},
    {"\xE2\x80\x9C", 14},  // left double quotation "
    {"\xE2\x80\x9D", 15},  // right double quotation "
    {" ",  16},
    {"\xCC\x83", 17},   // combining tilde ̃
    {"\xCA\xA3", 18},   // ʣ
    {"\xCA\xA5", 19},   // ʥ
    {"\xCA\xA6", 20},   // ʦ
    {"\xCA\xA8", 21},   // ʨ
    {"\xE1\xB5\x9D", 22},  // ᵝ
    {"\xEA\xAD\xA7", 23},  // ꭧ
    {"A",  24}, {"I",  25}, {"O",  31}, {"Q",  33},
    {"S",  35}, {"T",  36}, {"W",  39}, {"Y",  41},
    {"\xE1\xB5\x8A", 42},  // ᵊ
    {"a",  43}, {"b",  44}, {"c",  45}, {"d",  46}, {"e",  47},
    {"f",  48}, {"g",  49}, {"h",  50}, {"i",  51}, {"j",  52},
    {"k",  53}, {"l",  54}, {"m",  55}, {"n",  56}, {"o",  57},
    {"p",  58}, {"q",  59}, {"r",  60}, {"s",  61}, {"t",  62},
    {"u",  63}, {"v",  64}, {"w",  65}, {"x",  66}, {"y",  67},
    {"z",  68},
    {"\xC9\x91", 69},   // ɑ
    {"\xC9\x90", 70},   // ɐ
    {"\xC9\x92", 71},   // ɒ
    {"\xC3\xA6", 72},   // æ
    {"\xCE\xB2", 75},   // β
    {"\xC9\x94", 76},   // ɔ
    {"\xC9\x95", 77},   // ɕ
    {"\xC3\xA7", 78},   // ç
    {"\xC9\x96", 80},   // ɖ
    {"\xC3\xB0", 81},   // ð
    {"\xCA\xA4", 82},   // ʤ
    {"\xC9\x99", 83},   // ə
    {"\xC9\x9A", 85},   // ɚ
    {"\xC9\x9B", 86},   // ɛ
    {"\xC9\x9C", 87},   // ɜ
    {"\xC9\x9F", 90},   // ɟ
    {"\xC9\xA1", 92},   // ɡ
    {"\xC9\xA5", 99},   // ɥ
    {"\xC9\xA8", 101},  // ɨ
    {"\xC9\xAA", 102},  // ɪ
    {"\xCA\x9D", 103},  // ʝ
    {"\xC9\xB0", 111},  // ɰ
    {"\xC5\x8B", 112},  // ŋ
    {"\xC9\xB3", 113},  // ɳ
    {"\xC9\xB2", 114},  // ɲ
    {"\xC9\xB4", 115},  // ɴ
    {"\xC3\xB8", 116},  // ø
    {"\xC9\xB8", 118},  // ɸ
    {"\xCE\xB8", 119},  // θ
    {"\xC5\x93", 120},  // œ
    {"\xC9\xB9", 123},  // ɹ
    {"\xC9\xBE", 125},  // ɾ
    {"\xC9\xBB", 126},  // ɻ
    {"\xCA\x81", 128},  // ʁ
    {"\xC9\xBD", 129},  // ɽ
    {"\xCA\x82", 130},  // ʂ
    {"\xCA\x83", 131},  // ʃ
    {"\xCA\x88", 132},  // ʈ
    {"\xCA\xA7", 133},  // ʧ
    {"\xCA\x8A", 135},  // ʊ
    {"\xCA\x8B", 136},  // ʋ
    {"\xCA\x8C", 138},  // ʌ
    {"\xC9\xA3", 139},  // ɣ
    {"\xC9\xA4", 140},  // ɤ
    {"\xCF\x87", 142},  // χ
    {"\xCA\x8E", 143},  // ʎ
    {"\xCA\x92", 147},  // ʒ
    {"\xCB\x88", 156},  // ˈ primary stress
    {"\xCB\x8C", 157},  // ˌ secondary stress
    {"\xCB\x90", 158},  // ː long vowel
    {"\xCB\xB0", 162},  // ʰ aspiration
    {"\xCB\xB2", 164},  // ʲ palatalization
    {"\xE2\x86\x93", 169},  // ↓ downstep
    {"\xE2\x86\x92", 171},  // → level
    {"\xE2\x86\x97", 172},  // ↗ rise
    {"\xE2\x86\x98", 173},  // ↘ fall
    {"\xE1\xB4\xBB", 177},  // ᵻ
};

static const std::array<const char*, 3> INPUT_NAMES  = {"input_ids", "style", "speed"};
static const std::array<const char*, 1> OUTPUT_NAMES = {"output"};

std::vector<int64_t> encode_phonemes(const std::string& phonemes) {
    std::vector<std::string> chars = utf8_chars(phonemes);
    std::vector<int64_t> ids;
    ids.reserve(chars.size() + 2);
    ids.push_back(0); // wrap start
    for (const auto& ch : chars) {
        auto it = KOKORO_VOCAB.find(ch);
        ids.push_back(it != KOKORO_VOCAB.end() ? (int64_t)it->second : 0);
    }
    ids.push_back(0); // wrap end
    return ids;
}

Session::Session(const std::string& model_path)
    : env(ORT_LOGGING_LEVEL_WARNING, "Kokoro"), session(nullptr)
{
    env.DisableTelemetryEvents();

    Ort::SessionOptions opts;
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    session = new Ort::Session(env, (const ORTCHAR_T*)model_path.c_str(), opts);
}

Session::~Session() {
    delete session;
}

std::vector<float> Session::load_voice_style(const std::string& voice_path, int n_tokens) {
    std::ifstream f(voice_path, std::ios::binary);
    if (!f.is_open()) {
        throw std::runtime_error("[Kokoro] Could not open voice file: " + voice_path);
    }

    // Read all float32 values (little-endian)
    f.seekg(0, std::ios::end);
    size_t byte_size = f.tellg();
    f.seekg(0, std::ios::beg);

    size_t float_count = byte_size / 4;
    std::vector<float> all_floats(float_count);
    f.read(reinterpret_cast<char*>(all_floats.data()), byte_size);

    // Index style vector: row = n_tokens, each row has STYLE_DIM floats
    int offset = n_tokens * STYLE_DIM;
    if (offset + STYLE_DIM <= (int)float_count) {
        return std::vector<float>(all_floats.begin() + offset,
                                  all_floats.begin() + offset + STYLE_DIM);
    }
    // Fallback: last available vector
    int safe = std::max(0, (int)float_count - STYLE_DIM);
    return std::vector<float>(all_floats.begin() + safe,
                              all_floats.begin() + safe + STYLE_DIM);
}

void Session::tts(
    const std::string& phonemes,
    const std::string& voice_path,
    float speed,
    const std::string& output_path
) {
    // 1. Encode phonemes to token IDs
    std::vector<int64_t> ids = encode_phonemes(phonemes);

    // n_tokens: phoneme count excluding the two wrapping special tokens, capped
    int n_tokens = std::min(std::max((int)ids.size() - 2, 0), MAX_PHONEME_LENGTH - 1);

    // 2. Load voice style vector
    std::vector<float> style = load_voice_style(voice_path, n_tokens);
    if ((int)style.size() != STYLE_DIM) {
        throw std::runtime_error("[Kokoro] Voice style vector has wrong size.");
    }

    // 3. Build ONNX inputs
    Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::vector<Ort::Value> inputs;

    // input_ids: [1, N]
    std::vector<int64_t> ids_shape = {1, (int64_t)ids.size()};
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(
        mem_info, ids.data(), ids.size(), ids_shape.data(), ids_shape.size()
    ));

    // style: [1, STYLE_DIM]
    std::vector<int64_t> style_shape = {1, STYLE_DIM};
    inputs.push_back(Ort::Value::CreateTensor<float>(
        mem_info, style.data(), style.size(), style_shape.data(), style_shape.size()
    ));

    // speed: [1]
    float speed_val = speed;
    std::vector<int64_t> speed_shape = {1};
    inputs.push_back(Ort::Value::CreateTensor<float>(
        mem_info, &speed_val, 1, speed_shape.data(), speed_shape.size()
    ));

    // 4. Run inference
    auto outputs = session->Run(
        Ort::RunOptions{nullptr},
        INPUT_NAMES.data(), inputs.data(), INPUT_NAMES.size(),
        OUTPUT_NAMES.data(), OUTPUT_NAMES.size()
    );

    if (outputs.empty()) throw std::runtime_error("[Kokoro] No output tensor.");

    const float* waveform = outputs.front().GetTensorData<float>();
    auto out_shape = outputs.front().GetTensorTypeAndShapeInfo().GetShape();
    int64_t n = out_shape.back();

    // 5. Convert float32 [-1, 1] → int16 PCM
    std::vector<int16_t> pcm(n);
    for (int64_t i = 0; i < n; ++i) {
        float v = waveform[i];
        if (v < -1.0f) v = -1.0f;
        if (v >  1.0f) v =  1.0f;
        pcm[i] = (int16_t)(v * 32767.0f);
    }

    write_wav(output_path, pcm.data(), pcm.size(), SAMPLE_RATE);
}

} // namespace Kokoro
