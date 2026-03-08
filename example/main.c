#include "babylon.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: babylon <text>\n");
        return 1;
    }

    const char* text = argv[1];

    // Initialize OpenPhonemizer with optional dictionary
    babylon_g2p_options_t g2p_opts = {
        .dictionary_path  = "./models/dictionary.json",
        .use_punctuation  = 1,
    };

    if (babylon_g2p_init("./models/open-phonemizer.onnx", g2p_opts) != 0) {
        fprintf(stderr, "Failed to initialize phonemizer\n");
        return 1;
    }

    // Phonemize text
    char* phonemes = babylon_g2p(text);
    if (phonemes) {
        printf("Phonemes: %s\n", phonemes);
        free(phonemes);
    }

    // VITS TTS
    if (babylon_tts_init("./models/amy.onnx") == 0) {
        babylon_tts(text, "./c_output.wav");
        babylon_tts_free();
    }

    // Kitten (Kokoro) TTS
    if (babylon_kitten_init("./models/kokoro-quantized.onnx") == 0) {
        babylon_kitten_tts(text, "./models/voices/en-US-heart-kokoro.bin", 1.0f, "./kitten_output.wav");
        babylon_kitten_free();
    }

    babylon_g2p_free();
    return 0;
}
