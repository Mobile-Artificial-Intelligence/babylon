#include "babylon.h"
#include "path_utils.h"
#include <onnxruntime_cxx_api.h>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <cstdint>
#include <cctype>
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
    std::ofstream f(BabylonPath::filesystem_path(path), std::ios::binary);
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

namespace {

static bool session_has_output(Ort::Session& session, const std::string& output_name) {
    Ort::AllocatorWithDefaultOptions alloc;
    size_t output_count = session.GetOutputCount();
    for (size_t i = 0; i < output_count; ++i) {
        auto name = session.GetOutputNameAllocated(i, alloc);
        if (name && output_name == name.get()) return true;
    }
    return false;
}

static std::filesystem::path timing_variant_path(const std::string& model_path) {
    std::filesystem::path path = BabylonPath::filesystem_path(model_path);
    std::string timing_name = path.stem().u8string();
    timing_name += ".timing";
    timing_name += path.extension().u8string();
    return path.parent_path() / BabylonPath::filesystem_path(timing_name);
}

static Ort::Session* load_session_with_optional_timing(
    Ort::Env& env,
    const Ort::SessionOptions& opts,
    const std::string& model_path,
    bool& timing_available
) {
    std::filesystem::path primary_path = BabylonPath::filesystem_path(model_path);
    BabylonPath::OrtPathString ort_primary_path = BabylonPath::ort_path(primary_path);
    Ort::Session* primary = new Ort::Session(env, ort_primary_path.c_str(), opts);
    if (session_has_output(*primary, "duration")) {
        timing_available = true;
        return primary;
    }

    std::filesystem::path timing_path = timing_variant_path(model_path);
    if (timing_path != primary_path && std::filesystem::exists(timing_path)) {
        BabylonPath::OrtPathString ort_timing_path = BabylonPath::ort_path(timing_path);
        Ort::Session* patched = new Ort::Session(env, ort_timing_path.c_str(), opts);
        if (session_has_output(*patched, "duration")) {
            delete primary;
            timing_available = true;
            return patched;
        }
        delete patched;
    }

    timing_available = false;
    return primary;
}

static std::vector<int64_t> extract_duration_units(const Ort::Value& tensor) {
    auto info = tensor.GetTensorTypeAndShapeInfo();
    size_t count = info.GetElementCount();
    ONNXTensorElementDataType type = info.GetElementType();

    std::vector<int64_t> values(count);
    if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
        const int64_t* data = tensor.GetTensorData<int64_t>();
        std::copy(data, data + count, values.begin());
        return values;
    }
    if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        const float* data = tensor.GetTensorData<float>();
        for (size_t i = 0; i < count; ++i) values[i] = static_cast<int64_t>(std::llround(data[i]));
        return values;
    }

    throw std::runtime_error("Unsupported duration tensor type.");
}

static Babylon::TimingTrace build_timing_trace(
    int sample_rate,
    int64_t samples_per_unit,
    const std::vector<std::string>& labels,
    const std::vector<Babylon::TimingKind>& kinds,
    const std::vector<int64_t>& duration_units
) {
    if (labels.size() != duration_units.size() || labels.size() != kinds.size()) {
        throw std::runtime_error("Timing labels and duration tensors do not align.");
    }

    Babylon::TimingTrace trace{};
    trace.sample_rate = sample_rate;
    trace.samples_per_unit = samples_per_unit;
    trace.audio_samples = 0;

    int64_t cursor = 0;
    for (size_t i = 0; i < labels.size(); ++i) {
        int64_t duration_samples = duration_units[i] * samples_per_unit;
        Babylon::TimingItem item{};
        item.token = labels[i];
        item.kind = kinds[i];
        item.start_sample = cursor;
        item.end_sample = cursor + duration_samples;
        item.duration_samples = duration_samples;
        item.duration_units = duration_units[i];
        item.start_seconds = static_cast<double>(item.start_sample) / sample_rate;
        item.end_seconds = static_cast<double>(item.end_sample) / sample_rate;
        item.duration_seconds = static_cast<double>(item.duration_samples) / sample_rate;
        trace.items.push_back(item);
        cursor = item.end_sample;
    }

    trace.audio_samples = cursor;
    return trace;
}

static void trim_timing_trace(Babylon::TimingTrace& trace, int64_t audio_samples) {
    if (audio_samples < 0) audio_samples = 0;
    if (audio_samples >= trace.audio_samples) {
        trace.audio_samples = audio_samples;
        return;
    }

    trace.audio_samples = audio_samples;
    for (auto& item : trace.items) {
        int64_t clipped_start = std::min(item.start_sample, audio_samples);
        int64_t clipped_end = std::min(item.end_sample, audio_samples);
        if (clipped_end < clipped_start) clipped_end = clipped_start;

        item.start_sample = clipped_start;
        item.end_sample = clipped_end;
        item.duration_samples = clipped_end - clipped_start;
        item.duration_seconds = static_cast<double>(item.duration_samples) / trace.sample_rate;
        item.start_seconds = static_cast<double>(item.start_sample) / trace.sample_rate;
        item.end_seconds = static_cast<double>(item.end_sample) / trace.sample_rate;
    }
}

static bool is_ascii_punctuation(const std::string& token) {
    return token.size() == 1 && std::ispunct(static_cast<unsigned char>(token[0])) != 0;
}

static Babylon::TimingKind classify_visible_token(const std::string& token) {
    if (token == " ") return Babylon::TimingKind::Space;
    if (
        is_ascii_punctuation(token) ||
        token == "\xE2\x80\x94" ||
        token == "\xE2\x80\xA6" ||
        token == "\xE2\x80\x9C" ||
        token == "\xE2\x80\x9D"
    ) {
        return Babylon::TimingKind::Punctuation;
    }
    return Babylon::TimingKind::Phoneme;
}

} // namespace

// ---------------------------------------------------------------------------
// VITS namespace
// ---------------------------------------------------------------------------

namespace Vits {

static const std::array<const char*, 3> INPUT_NAMES  = {"input", "input_lengths", "scales"};
static const int64_t SAMPLES_PER_UNIT = 256;

static const float FMIN = static_cast<float>(std::numeric_limits<int16_t>::min());
static const float FMAX = static_cast<float>(std::numeric_limits<int16_t>::max());

struct InferenceResult {
    std::vector<float> audio;
    std::vector<int64_t> duration_units;
};

static std::vector<std::string> build_labels(
    const SequenceTokenizer& tokenizer,
    const std::vector<std::string>& phonemes
) {
    std::vector<std::string> labels = {"<bos>", "<blank>"};
    for (const auto& phoneme : phonemes) {
        if (!tokenizer.has_token(phoneme)) continue;
        labels.push_back(phoneme);
        labels.push_back("<blank>");
    }
    labels.push_back("<eos>");
    return labels;
}

static std::vector<Babylon::TimingKind> build_kinds(const std::vector<std::string>& labels) {
    std::vector<Babylon::TimingKind> kinds;
    kinds.reserve(labels.size());
    for (const auto& label : labels) {
        if (label == "<bos>" || label == "<eos>") {
            kinds.push_back(Babylon::TimingKind::Special);
        } else if (label == "<blank>") {
            kinds.push_back(Babylon::TimingKind::Blank);
        } else {
            kinds.push_back(classify_visible_token(label));
        }
    }
    return kinds;
}

static InferenceResult run_inference(
    Ort::Session& session,
    bool timing_available,
    SequenceTokenizer& tokenizer,
    const std::vector<float>& scales,
    const std::vector<std::string>& phonemes
) {
    Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::vector<int64_t> ids = tokenizer(phonemes);
    std::vector<int64_t> shape = {1, (int64_t)ids.size()};
    std::vector<Ort::Value> inputs;

    inputs.push_back(Ort::Value::CreateTensor<int64_t>(
        mem_info, ids.data(), ids.size(), shape.data(), shape.size()
    ));

    std::vector<int64_t> len_val = {(int64_t)ids.size()};
    std::vector<int64_t> len_shape = {1};
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(
        mem_info, len_val.data(), len_val.size(), len_shape.data(), len_shape.size()
    ));

    std::vector<int64_t> scales_shape = {(int64_t)scales.size()};
    inputs.push_back(Ort::Value::CreateTensor<float>(
        mem_info, const_cast<float*>(scales.data()), scales.size(), scales_shape.data(), scales_shape.size()
    ));

    std::vector<const char*> output_names = {"output"};
    if (timing_available) output_names.push_back("duration");

    auto outputs = session.Run(
        Ort::RunOptions{nullptr},
        INPUT_NAMES.data(), inputs.data(), INPUT_NAMES.size(),
        output_names.data(), output_names.size()
    );

    if (outputs.empty()) throw std::runtime_error("[VITS] No output tensor.");

    const float* out = outputs.front().GetTensorData<float>();
    size_t sample_count = outputs.front().GetTensorTypeAndShapeInfo().GetElementCount();

    InferenceResult result;
    result.audio.assign(out, out + sample_count);
    if (timing_available && outputs.size() > 1) {
        result.duration_units = extract_duration_units(outputs[1]);
    }

    return result;
}

static void write_audio(const std::vector<float>& audio, int sample_rate, const std::string& output_path) {
    int64_t n = static_cast<int64_t>(audio.size());

    // Normalize to int16 range
    float peak = 0.01f;
    for (int64_t i = 0; i < n; ++i) {
        float v = std::abs(audio[i]);
        if (v > peak) peak = v;
    }
    float scale = 32767.0f / peak;

    std::vector<int16_t> pcm(n);
    for (int64_t i = 0; i < n; ++i) {
        float v = audio[i] * scale;
        if (v < FMIN) v = FMIN;
        if (v > FMAX) v = FMAX;
        pcm[i] = (int16_t)v;
    }

    write_wav(output_path, pcm.data(), pcm.size(), (uint32_t)sample_rate);
}

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

bool SequenceTokenizer::has_token(const std::string& phoneme) const {
    return token_to_idx.find(phoneme) != token_to_idx.end();
}

Session::Session(const std::string& model_path)
    : sample_rate(0), timing_available(false),
      env(ORT_LOGGING_LEVEL_WARNING, "VITS"), session(nullptr), phoneme_tokenizer(nullptr)
{
    env.DisableTelemetryEvents();

    Ort::SessionOptions opts;
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    opts.DisableMemPattern();

    session = load_session_with_optional_timing(env, opts, model_path, timing_available);

    Ort::ModelMetadata meta = session->GetModelMetadata();
    Ort::AllocatorWithDefaultOptions alloc;

    std::string phoneme_str = meta.LookupCustomMetadataMapAllocated("phonemes", alloc).get();
    std::vector<std::string> phonemes;
    std::istringstream phoneme_ss(phoneme_str);
    std::string buf;
    while (phoneme_ss >> buf) {
        phonemes.push_back(buf == "<space>" ? " " : buf);
    }

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
    InferenceResult result = run_inference(*session, timing_available, *phoneme_tokenizer, scales, phonemes);
    write_audio(result.audio, sample_rate, output_path);
}

Babylon::TimingTrace Session::tts_with_timings(
    const std::vector<std::string>& phonemes,
    const std::string& output_path
) {
    if (!timing_available) {
        throw std::runtime_error(
            "[VITS] Timing output unavailable. Replace the model with a timing-enabled export or patch it in place with scripts/onnx/add_timing_output.py."
        );
    }

    InferenceResult result = run_inference(*session, timing_available, *phoneme_tokenizer, scales, phonemes);
    write_audio(result.audio, sample_rate, output_path);

    std::vector<std::string> labels = build_labels(*phoneme_tokenizer, phonemes);
    std::vector<Babylon::TimingKind> kinds = build_kinds(labels);
    Babylon::TimingTrace trace = build_timing_trace(
        sample_rate, SAMPLES_PER_UNIT, labels, kinds, result.duration_units
    );
    trace.audio_samples = static_cast<int64_t>(result.audio.size());
    return trace;
}

Babylon::TimingTrace Session::timings(const std::vector<std::string>& phonemes) {
    if (!timing_available) {
        throw std::runtime_error(
            "[VITS] Timing output unavailable. Replace the model with a timing-enabled export or patch it in place with scripts/onnx/add_timing_output.py."
        );
    }

    InferenceResult result = run_inference(*session, timing_available, *phoneme_tokenizer, scales, phonemes);
    std::vector<std::string> labels = build_labels(*phoneme_tokenizer, phonemes);
    std::vector<Babylon::TimingKind> kinds = build_kinds(labels);
    Babylon::TimingTrace trace = build_timing_trace(
        sample_rate, SAMPLES_PER_UNIT, labels, kinds, result.duration_units
    );
    trace.audio_samples = static_cast<int64_t>(result.audio.size());
    return trace;
}

bool Session::supports_timings() const {
    return timing_available;
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
static const int64_t SAMPLES_PER_UNIT = 600;
static const int STYLE_DIM = 256;
static const int MAX_PHONEME_LENGTH = 510;
static const int SAMPLE_RATE = 24000;

struct InferenceResult {
    std::vector<float> audio;
    std::vector<int64_t> duration_units;
};

static std::vector<std::string> build_labels(const std::string& phonemes) {
    std::vector<std::string> labels = {"<bos>"};
    std::vector<std::string> chars = utf8_chars(phonemes);
    labels.insert(labels.end(), chars.begin(), chars.end());
    labels.push_back("<eos>");
    return labels;
}

static std::vector<Babylon::TimingKind> build_kinds(const std::vector<std::string>& labels) {
    std::vector<Babylon::TimingKind> kinds;
    kinds.reserve(labels.size());
    for (const auto& label : labels) {
        if (label == "<bos>" || label == "<eos>") {
            kinds.push_back(Babylon::TimingKind::Special);
        } else {
            kinds.push_back(classify_visible_token(label));
        }
    }
    return kinds;
}

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
    : timing_available(false), env(ORT_LOGGING_LEVEL_WARNING, "Kokoro"), session(nullptr)
{
    env.DisableTelemetryEvents();

    Ort::SessionOptions opts;
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    session = load_session_with_optional_timing(env, opts, model_path, timing_available);
}

Session::~Session() {
    delete session;
}

std::vector<float> Session::load_voice_style(const std::string& voice_path, int n_tokens) {
    std::ifstream f(BabylonPath::filesystem_path(voice_path), std::ios::binary);
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

static InferenceResult run_inference(
    Ort::Session& session,
    bool timing_available,
    const std::string& phonemes,
    const std::string& voice_path,
    float speed
) {
    std::vector<int64_t> ids = encode_phonemes(phonemes);

    int n_tokens = std::min(std::max((int)ids.size() - 2, 0), MAX_PHONEME_LENGTH - 1);

    std::vector<float> style;
    {
        std::ifstream f(BabylonPath::filesystem_path(voice_path), std::ios::binary);
        if (!f.is_open()) {
            throw std::runtime_error("[Kokoro] Could not open voice file: " + voice_path);
        }

        f.seekg(0, std::ios::end);
        size_t byte_size = f.tellg();
        f.seekg(0, std::ios::beg);

        size_t float_count = byte_size / 4;
        std::vector<float> all_floats(float_count);
        f.read(reinterpret_cast<char*>(all_floats.data()), byte_size);

        int offset = n_tokens * STYLE_DIM;
        if (offset + STYLE_DIM <= (int)float_count) {
            style = std::vector<float>(all_floats.begin() + offset,
                                       all_floats.begin() + offset + STYLE_DIM);
        } else {
            int safe = std::max(0, (int)float_count - STYLE_DIM);
            style = std::vector<float>(all_floats.begin() + safe,
                                       all_floats.begin() + safe + STYLE_DIM);
        }
    }

    if ((int)style.size() != STYLE_DIM) {
        throw std::runtime_error("[Kokoro] Voice style vector has wrong size.");
    }

    Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<Ort::Value> inputs;

    std::vector<int64_t> ids_shape = {1, (int64_t)ids.size()};
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(
        mem_info, ids.data(), ids.size(), ids_shape.data(), ids_shape.size()
    ));

    std::vector<int64_t> style_shape = {1, STYLE_DIM};
    inputs.push_back(Ort::Value::CreateTensor<float>(
        mem_info, style.data(), style.size(), style_shape.data(), style_shape.size()
    ));

    float speed_val = speed;
    std::vector<int64_t> speed_shape = {1};
    inputs.push_back(Ort::Value::CreateTensor<float>(
        mem_info, &speed_val, 1, speed_shape.data(), speed_shape.size()
    ));

    std::vector<const char*> output_names = {"waveform"};
    if (timing_available) output_names.push_back("duration");

    auto outputs = session.Run(
        Ort::RunOptions{nullptr},
        INPUT_NAMES.data(), inputs.data(), INPUT_NAMES.size(),
        output_names.data(), output_names.size()
    );

    if (outputs.empty()) throw std::runtime_error("[Kokoro] No output tensor.");

    const float* waveform = outputs.front().GetTensorData<float>();
    size_t sample_count = outputs.front().GetTensorTypeAndShapeInfo().GetElementCount();

    InferenceResult result;
    result.audio.assign(waveform, waveform + sample_count);
    if (timing_available && outputs.size() > 1) {
        result.duration_units = extract_duration_units(outputs[1]);
    }
    return result;
}

static void write_audio(const std::vector<float>& audio, const std::string& output_path) {
    int64_t n = static_cast<int64_t>(audio.size());

    std::vector<int16_t> pcm(n);
    for (int64_t i = 0; i < n; ++i) {
        float v = audio[i];
        if (v < -1.0f) v = -1.0f;
        if (v >  1.0f) v =  1.0f;
        pcm[i] = (int16_t)(v * 32767.0f);
    }

    write_wav(output_path, pcm.data(), pcm.size(), SAMPLE_RATE);
}

void Session::tts(
    const std::string& phonemes,
    const std::string& voice_path,
    float speed,
    const std::string& output_path
) {
    InferenceResult result = run_inference(*session, timing_available, phonemes, voice_path, speed);
    write_audio(result.audio, output_path);
}

Babylon::TimingTrace Session::tts_with_timings(
    const std::string& phonemes,
    const std::string& voice_path,
    float speed,
    const std::string& output_path
) {
    if (!timing_available) {
        throw std::runtime_error(
            "[Kokoro] Timing output unavailable. Replace the model with a timing-enabled export or patch it in place with scripts/onnx/add_timing_output.py."
        );
    }

    InferenceResult result = run_inference(*session, timing_available, phonemes, voice_path, speed);
    write_audio(result.audio, output_path);

    std::vector<std::string> labels = build_labels(phonemes);
    std::vector<Babylon::TimingKind> kinds = build_kinds(labels);
    Babylon::TimingTrace trace = build_timing_trace(
        SAMPLE_RATE, SAMPLES_PER_UNIT, labels, kinds, result.duration_units
    );
    trace.audio_samples = static_cast<int64_t>(result.audio.size());
    return trace;
}

Babylon::TimingTrace Session::timings(
    const std::string& phonemes,
    const std::string& voice_path,
    float speed
) {
    if (!timing_available) {
        throw std::runtime_error(
            "[Kokoro] Timing output unavailable. Replace the model with a timing-enabled export or patch it in place with scripts/onnx/add_timing_output.py."
        );
    }

    InferenceResult result = run_inference(*session, timing_available, phonemes, voice_path, speed);
    std::vector<std::string> labels = build_labels(phonemes);
    std::vector<Babylon::TimingKind> kinds = build_kinds(labels);
    Babylon::TimingTrace trace = build_timing_trace(
        SAMPLE_RATE, SAMPLES_PER_UNIT, labels, kinds, result.duration_units
    );
    trace.audio_samples = static_cast<int64_t>(result.audio.size());
    return trace;
}

bool Session::supports_timings() const {
    return timing_available;
}

} // namespace Kokoro

// ---------------------------------------------------------------------------
// Kitten namespace
// ---------------------------------------------------------------------------

namespace Kitten {

static const std::array<const char*, 3> INPUT_NAMES  = {"input_ids", "style", "speed"};
static const int64_t SAMPLES_PER_UNIT = 600;
static const int STYLE_DIM = 256;
static const int SAMPLE_RATE = 24000;
static const int TRIM_SAMPLES = 5000;
static const int64_t END_MARKER = 10;

struct InferenceResult {
    std::vector<float> audio;
    std::vector<int64_t> duration_units;
};

static std::vector<std::string> build_labels(const std::string& phonemes) {
    std::vector<std::string> labels = {"<bos>"};
    std::vector<std::string> chars = utf8_chars(phonemes);
    labels.insert(labels.end(), chars.begin(), chars.end());
    labels.push_back("<stop>");
    labels.push_back("<eos>");
    return labels;
}

static std::vector<Babylon::TimingKind> build_kinds(const std::vector<std::string>& labels) {
    std::vector<Babylon::TimingKind> kinds;
    kinds.reserve(labels.size());
    for (const auto& label : labels) {
        if (label == "<bos>" || label == "<stop>" || label == "<eos>") {
            kinds.push_back(Babylon::TimingKind::Special);
        } else {
            kinds.push_back(classify_visible_token(label));
        }
    }
    return kinds;
}

static int64_t trimmed_audio_samples(const std::vector<float>& audio) {
    if (audio.size() <= static_cast<size_t>(TRIM_SAMPLES)) {
        return static_cast<int64_t>(audio.size());
    }
    return static_cast<int64_t>(audio.size() - TRIM_SAMPLES);
}

std::vector<int64_t> encode_phonemes(const std::string& phonemes) {
    std::vector<std::string> chars = utf8_chars(phonemes);
    std::vector<int64_t> ids;
    ids.reserve(chars.size() + 3);
    ids.push_back(0);
    for (const auto& ch : chars) {
        auto it = Kokoro::KOKORO_VOCAB.find(ch);
        ids.push_back(it != Kokoro::KOKORO_VOCAB.end() ? static_cast<int64_t>(it->second) : 0);
    }
    ids.push_back(END_MARKER);
    ids.push_back(0);
    return ids;
}

Session::Session(const std::string& model_path)
    : timing_available(false), env(ORT_LOGGING_LEVEL_WARNING, "Kitten"), session(nullptr)
{
    env.DisableTelemetryEvents();

    Ort::SessionOptions opts;
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    session = load_session_with_optional_timing(env, opts, model_path, timing_available);
}

Session::~Session() {
    delete session;
}

std::vector<float> Session::load_voice_style(const std::string& voice_path, size_t phoneme_length) {
    std::ifstream f(BabylonPath::filesystem_path(voice_path), std::ios::binary);
    if (!f.is_open()) {
        throw std::runtime_error("[Kitten] Could not open voice file: " + voice_path);
    }

    f.seekg(0, std::ios::end);
    size_t byte_size = f.tellg();
    f.seekg(0, std::ios::beg);

    size_t float_count = byte_size / 4;
    if (float_count < STYLE_DIM) {
        throw std::runtime_error("[Kitten] Voice style vector has wrong size.");
    }

    std::vector<float> all_floats(float_count);
    f.read(reinterpret_cast<char*>(all_floats.data()), byte_size);

    size_t rows = float_count / STYLE_DIM;
    size_t ref_id = std::min(phoneme_length, rows - 1);
    size_t offset = ref_id * STYLE_DIM;
    return std::vector<float>(all_floats.begin() + offset, all_floats.begin() + offset + STYLE_DIM);
}

static InferenceResult run_inference(
    Ort::Session& session,
    bool timing_available,
    const std::vector<int64_t>& ids,
    const std::vector<float>& style,
    float speed
) {
    Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<Ort::Value> inputs;

    std::vector<int64_t> ids_shape = {1, static_cast<int64_t>(ids.size())};
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(
        mem_info, const_cast<int64_t*>(ids.data()), ids.size(), ids_shape.data(), ids_shape.size()
    ));

    std::vector<int64_t> style_shape = {1, STYLE_DIM};
    inputs.push_back(Ort::Value::CreateTensor<float>(
        mem_info, const_cast<float*>(style.data()), style.size(), style_shape.data(), style_shape.size()
    ));

    float speed_val = speed;
    std::vector<int64_t> speed_shape = {1};
    inputs.push_back(Ort::Value::CreateTensor<float>(
        mem_info, &speed_val, 1, speed_shape.data(), speed_shape.size()
    ));

    std::vector<const char*> output_names = {"waveform"};
    if (timing_available) output_names.push_back("duration");

    auto outputs = session.Run(
        Ort::RunOptions{nullptr},
        INPUT_NAMES.data(), inputs.data(), INPUT_NAMES.size(),
        output_names.data(), output_names.size()
    );

    if (outputs.empty()) throw std::runtime_error("[Kitten] No output tensor.");

    const float* waveform = outputs.front().GetTensorData<float>();
    size_t sample_count = outputs.front().GetTensorTypeAndShapeInfo().GetElementCount();

    InferenceResult result;
    result.audio.assign(waveform, waveform + sample_count);
    if (timing_available && outputs.size() > 1) {
        result.duration_units = extract_duration_units(outputs[1]);
    }
    return result;
}

static void write_audio(const std::vector<float>& audio, const std::string& output_path) {
    int64_t n = trimmed_audio_samples(audio);
    std::vector<int16_t> pcm(n);
    for (int64_t i = 0; i < n; ++i) {
        float v = audio[i];
        if (v < -1.0f) v = -1.0f;
        if (v >  1.0f) v =  1.0f;
        pcm[i] = static_cast<int16_t>(v * 32767.0f);
    }

    write_wav(output_path, pcm.data(), pcm.size(), SAMPLE_RATE);
}

void Session::tts(
    const std::string& phonemes,
    const std::string& voice_path,
    float speed,
    const std::string& output_path
) {
    std::vector<int64_t> ids = encode_phonemes(phonemes);
    std::vector<float> style = load_voice_style(voice_path, utf8_chars(phonemes).size());
    InferenceResult result = run_inference(*session, timing_available, ids, style, speed);
    write_audio(result.audio, output_path);
}

Babylon::TimingTrace Session::tts_with_timings(
    const std::string& phonemes,
    const std::string& voice_path,
    float speed,
    const std::string& output_path
) {
    if (!timing_available) {
        throw std::runtime_error(
            "[Kitten] Timing output unavailable. Replace the model with a timing-enabled export or patch it in place with scripts/onnx/add_timing_output.py."
        );
    }

    std::vector<int64_t> ids = encode_phonemes(phonemes);
    std::vector<float> style = load_voice_style(voice_path, utf8_chars(phonemes).size());
    InferenceResult result = run_inference(*session, timing_available, ids, style, speed);
    write_audio(result.audio, output_path);

    std::vector<std::string> labels = build_labels(phonemes);
    std::vector<Babylon::TimingKind> kinds = build_kinds(labels);
    Babylon::TimingTrace trace = build_timing_trace(
        SAMPLE_RATE, SAMPLES_PER_UNIT, labels, kinds, result.duration_units
    );
    trim_timing_trace(trace, trimmed_audio_samples(result.audio));
    return trace;
}

Babylon::TimingTrace Session::timings(
    const std::string& phonemes,
    const std::string& voice_path,
    float speed
) {
    if (!timing_available) {
        throw std::runtime_error(
            "[Kitten] Timing output unavailable. Replace the model with a timing-enabled export or patch it in place with scripts/onnx/add_timing_output.py."
        );
    }

    std::vector<int64_t> ids = encode_phonemes(phonemes);
    std::vector<float> style = load_voice_style(voice_path, utf8_chars(phonemes).size());
    InferenceResult result = run_inference(*session, timing_available, ids, style, speed);
    std::vector<std::string> labels = build_labels(phonemes);
    std::vector<Babylon::TimingKind> kinds = build_kinds(labels);
    Babylon::TimingTrace trace = build_timing_trace(
        SAMPLE_RATE, SAMPLES_PER_UNIT, labels, kinds, result.duration_units
    );
    trim_timing_trace(trace, trimmed_audio_samples(result.audio));
    return trace;
}

bool Session::supports_timings() const {
    return timing_available;
}

} // namespace Kitten
