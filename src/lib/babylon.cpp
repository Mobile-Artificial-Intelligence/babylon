#include "babylon.h"
#include <iostream>
#include <cstring>
#include <cstdlib>

static const char* timing_kind_name(Babylon::TimingKind kind) {
    switch (kind) {
        case Babylon::TimingKind::Phoneme:    return "phoneme";
        case Babylon::TimingKind::Blank:      return "blank";
        case Babylon::TimingKind::Space:      return "space";
        case Babylon::TimingKind::Special:    return "special";
        case Babylon::TimingKind::Punctuation:return "punctuation";
    }
    return "phoneme";
}

static babylon_timing_result_t* copy_timing_trace(const Babylon::TimingTrace& trace) {
    babylon_timing_result_t* result = new babylon_timing_result_t{};
    result->sample_rate = trace.sample_rate;
    result->samples_per_unit = trace.samples_per_unit;
    result->audio_samples = trace.audio_samples;
    result->count = static_cast<long long>(trace.items.size());

    if (trace.items.empty()) {
        result->items = nullptr;
        return result;
    }

    result->items = new babylon_timing_item_t[trace.items.size()];
    for (size_t i = 0; i < trace.items.size(); ++i) {
        const Babylon::TimingItem& item = trace.items[i];
        babylon_timing_item_t& out = result->items[i];
        out.token = strdup(item.token.c_str());
        out.kind = strdup(timing_kind_name(item.kind));
        out.start_sample = item.start_sample;
        out.end_sample = item.end_sample;
        out.duration_samples = item.duration_samples;
        out.duration_units = item.duration_units;
        out.start_seconds = item.start_seconds;
        out.end_seconds = item.end_seconds;
        out.duration_seconds = item.duration_seconds;
    }

    return result;
}

static OpenPhonemizer::Session* op     = nullptr;
static Vits::Session*           vits   = nullptr;
static Kokoro::Session*         kokoro = nullptr;
static Kitten::Session*         kitten = nullptr;

extern "C" {

    BABYLON_EXPORT int babylon_g2p_init(const char* model_path, babylon_g2p_options_t options) {
        try {
            const std::string dict_path = options.dictionary_path ? options.dictionary_path : "";
            op = new OpenPhonemizer::Session(model_path, dict_path, options.use_punctuation != 0);
            return 0;
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] g2p init error: " << e.what() << std::endl;
            return 1;
        }
    }

    BABYLON_EXPORT char* babylon_g2p(const char* text) {
        if (!op) {
            std::cerr << "[babylon] OpenPhonemizer session not initialized." << std::endl;
            return nullptr;
        }
        try {
            std::string phonemes = op->phonemize(text);
            return strdup(phonemes.c_str());
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] g2p error: " << e.what() << std::endl;
            return nullptr;
        }
    }

    // Returns Kokoro-compatible token IDs, terminated by -1
    BABYLON_EXPORT int* babylon_g2p_tokens(const char* text) {
        if (!op) {
            std::cerr << "[babylon] OpenPhonemizer session not initialized." << std::endl;
            return nullptr;
        }
        try {
            std::vector<int64_t> ids = op->phonemize_tokens(text);
            ids.push_back(-1); // sentinel
            int* arr = new int[ids.size()];
            for (size_t i = 0; i < ids.size(); ++i) arr[i] = (int)ids[i];
            return arr;
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] g2p_tokens error: " << e.what() << std::endl;
            return nullptr;
        }
    }

    BABYLON_EXPORT void babylon_g2p_free(void) {
        delete op;
        op = nullptr;
    }

    BABYLON_EXPORT int babylon_tts_init(const char* model_path) {
        try {
            vits = new Vits::Session(model_path);
            return 0;
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] tts init error: " << e.what() << std::endl;
            return 1;
        }
    }

    BABYLON_EXPORT void babylon_tts(const char* text, const char* output_path) {
        if (!vits) {
            std::cerr << "[babylon] VITS session not initialized." << std::endl;
            return;
        }
        if (!op) {
            std::cerr << "[babylon] OpenPhonemizer session not initialized." << std::endl;
            return;
        }
        try {
            // Get IPA phoneme string, then split into individual unicode chars for VITS
            std::string phoneme_str = op->phonemize(text);
            std::vector<std::string> phonemes = utf8_chars(phoneme_str);
            vits->tts(phonemes, output_path);
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] tts error: " << e.what() << std::endl;
        }
    }

    BABYLON_EXPORT babylon_timing_result_t* babylon_tts_with_timings(const char* text, const char* output_path) {
        if (!vits) {
            std::cerr << "[babylon] VITS session not initialized." << std::endl;
            return nullptr;
        }
        if (!op) {
            std::cerr << "[babylon] OpenPhonemizer session not initialized." << std::endl;
            return nullptr;
        }
        try {
            std::string phoneme_str = op->phonemize(text);
            std::vector<std::string> phonemes = utf8_chars(phoneme_str);
            return copy_timing_trace(vits->tts_with_timings(phonemes, output_path));
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] tts with timings error: " << e.what() << std::endl;
            return nullptr;
        }
    }

    BABYLON_EXPORT babylon_timing_result_t* babylon_tts_timings(const char* text) {
        if (!vits) {
            std::cerr << "[babylon] VITS session not initialized." << std::endl;
            return nullptr;
        }
        if (!op) {
            std::cerr << "[babylon] OpenPhonemizer session not initialized." << std::endl;
            return nullptr;
        }
        try {
            std::string phoneme_str = op->phonemize(text);
            std::vector<std::string> phonemes = utf8_chars(phoneme_str);
            return copy_timing_trace(vits->timings(phonemes));
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] tts timings error: " << e.what() << std::endl;
            return nullptr;
        }
    }

    BABYLON_EXPORT void babylon_tts_free(void) {
        delete vits;
        vits = nullptr;
    }

    BABYLON_EXPORT int babylon_kokoro_init(const char* model_path) {
        try {
            kokoro = new Kokoro::Session(model_path);
            return 0;
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] kokoro init error: " << e.what() << std::endl;
            return 1;
        }
    }

    BABYLON_EXPORT void babylon_kokoro_tts(
        const char* text,
        const char* voice_path,
        float speed,
        const char* output_path
    ) {
        if (!kokoro) {
            std::cerr << "[babylon] Kokoro session not initialized." << std::endl;
            return;
        }
        if (!op) {
            std::cerr << "[babylon] OpenPhonemizer session not initialized." << std::endl;
            return;
        }
        try {
            std::string phonemes = op->phonemize(text);
            kokoro->tts(phonemes, voice_path, speed, output_path);
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] kokoro_tts error: " << e.what() << std::endl;
        }
    }

    BABYLON_EXPORT babylon_timing_result_t* babylon_kokoro_tts_with_timings(
        const char* text,
        const char* voice_path,
        float speed,
        const char* output_path
    ) {
        if (!kokoro) {
            std::cerr << "[babylon] Kokoro session not initialized." << std::endl;
            return nullptr;
        }
        if (!op) {
            std::cerr << "[babylon] OpenPhonemizer session not initialized." << std::endl;
            return nullptr;
        }
        try {
            std::string phonemes = op->phonemize(text);
            return copy_timing_trace(kokoro->tts_with_timings(phonemes, voice_path, speed, output_path));
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] kokoro tts with timings error: " << e.what() << std::endl;
            return nullptr;
        }
    }

    BABYLON_EXPORT babylon_timing_result_t* babylon_kokoro_timings(
        const char* text,
        const char* voice_path,
        float speed
    ) {
        if (!kokoro) {
            std::cerr << "[babylon] Kokoro session not initialized." << std::endl;
            return nullptr;
        }
        if (!op) {
            std::cerr << "[babylon] OpenPhonemizer session not initialized." << std::endl;
            return nullptr;
        }
        try {
            std::string phonemes = op->phonemize(text);
            return copy_timing_trace(kokoro->timings(phonemes, voice_path, speed));
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] kokoro timings error: " << e.what() << std::endl;
            return nullptr;
        }
    }

    BABYLON_EXPORT void babylon_kokoro_free(void) {
        delete kokoro;
        kokoro = nullptr;
    }

    BABYLON_EXPORT int babylon_kitten_init(const char* model_path) {
        try {
            kitten = new Kitten::Session(model_path);
            return 0;
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] kitten init error: " << e.what() << std::endl;
            return 1;
        }
    }

    BABYLON_EXPORT void babylon_kitten_tts(
        const char* text,
        const char* voice_path,
        float speed,
        const char* output_path
    ) {
        if (!kitten) {
            std::cerr << "[babylon] Kitten session not initialized." << std::endl;
            return;
        }
        if (!op) {
            std::cerr << "[babylon] OpenPhonemizer session not initialized." << std::endl;
            return;
        }
        try {
            std::string phonemes = op->phonemize(text);
            kitten->tts(phonemes, voice_path, speed, output_path);
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] kitten_tts error: " << e.what() << std::endl;
        }
    }

    BABYLON_EXPORT babylon_timing_result_t* babylon_kitten_tts_with_timings(
        const char* text,
        const char* voice_path,
        float speed,
        const char* output_path
    ) {
        if (!kitten) {
            std::cerr << "[babylon] Kitten session not initialized." << std::endl;
            return nullptr;
        }
        if (!op) {
            std::cerr << "[babylon] OpenPhonemizer session not initialized." << std::endl;
            return nullptr;
        }
        try {
            std::string phonemes = op->phonemize(text);
            return copy_timing_trace(kitten->tts_with_timings(phonemes, voice_path, speed, output_path));
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] kitten tts with timings error: " << e.what() << std::endl;
            return nullptr;
        }
    }

    BABYLON_EXPORT babylon_timing_result_t* babylon_kitten_timings(
        const char* text,
        const char* voice_path,
        float speed
    ) {
        if (!kitten) {
            std::cerr << "[babylon] Kitten session not initialized." << std::endl;
            return nullptr;
        }
        if (!op) {
            std::cerr << "[babylon] OpenPhonemizer session not initialized." << std::endl;
            return nullptr;
        }
        try {
            std::string phonemes = op->phonemize(text);
            return copy_timing_trace(kitten->timings(phonemes, voice_path, speed));
        }
        catch (const std::exception& e) {
            std::cerr << "[babylon] kitten timings error: " << e.what() << std::endl;
            return nullptr;
        }
    }

    BABYLON_EXPORT void babylon_kitten_free(void) {
        delete kitten;
        kitten = nullptr;
    }

    BABYLON_EXPORT void babylon_timing_result_free(babylon_timing_result_t* result) {
        if (!result) return;
        if (result->items) {
            for (long long i = 0; i < result->count; ++i) {
                std::free((void*)result->items[i].token);
                std::free((void*)result->items[i].kind);
            }
            delete[] result->items;
        }
        delete result;
    }

} // extern "C"
