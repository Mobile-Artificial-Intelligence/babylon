#include "babylon.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: babylon <text>" << std::endl;
        return 1;
    }

    std::string text = argv[1];
    std::string op_model   = "./models/open-phonemizer.onnx";
    std::string dict_path  = "./models/dictionary.json";
    std::string vits_model = "./models/curie.onnx";

    // OpenPhonemizer
    OpenPhonemizer::Session op(op_model, dict_path, true);

    std::string phonemes = op.phonemize(text);
    std::cout << "Phonemes: " << phonemes << std::endl;

    // VITS TTS
    Vits::Session vits(vits_model);
    std::vector<std::string> phoneme_chars = utf8_chars(phonemes);
    vits.tts(phoneme_chars, "./vits_output.wav");

    // Kokoro TTS
    Kokoro::Session kokoro("./models/kokoro-quantized.onnx");
    kokoro.tts(phonemes, "./models/voices/en-US-heart-kokoro.bin", 1.0f, "./kokoro_output.wav");

    // Kokoro token IDs
    std::vector<int64_t> ids = op.phonemize_tokens(text);
    std::cout << "Kokoro token IDs:";
    for (auto id : ids) std::cout << " " << id;
    std::cout << std::endl;

    return 0;
}
