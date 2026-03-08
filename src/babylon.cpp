#include "babylon.h"
#include <iostream>
#include <cstring>

static OpenPhonemizer::Session* op     = nullptr;
static Vits::Session*           vits   = nullptr;
static Kokoro::Session*         kokoro = nullptr;

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

    BABYLON_EXPORT void babylon_kokoro_free(void) {
        delete kokoro;
        kokoro = nullptr;
    }

} // extern "C"
