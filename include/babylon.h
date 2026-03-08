#ifndef BABYLON_H
#define BABYLON_H

#ifdef __cplusplus
#include <string>
#include <vector>
#include <unordered_map>
#include <onnxruntime_cxx_api.h>

extern "C" {
#endif

#ifdef WIN32
   #define BABYLON_EXPORT __declspec(dllexport)
#else
   #define BABYLON_EXPORT __attribute__((visibility("default"))) __attribute__((used))
#endif

typedef struct {
  const char* dictionary_path;
  const unsigned char use_punctuation;
} babylon_g2p_options_t;

BABYLON_EXPORT int babylon_g2p_init(const char* model_path, babylon_g2p_options_t options);

BABYLON_EXPORT char* babylon_g2p(const char* text);

// Returns Kokoro-compatible token IDs (int array terminated by -1)
BABYLON_EXPORT int* babylon_g2p_tokens(const char* text);

BABYLON_EXPORT void babylon_g2p_free(void);

BABYLON_EXPORT int babylon_tts_init(const char* model_path);

BABYLON_EXPORT void babylon_tts(const char* text, const char* output_path);

BABYLON_EXPORT void babylon_tts_free(void);

BABYLON_EXPORT int babylon_kokoro_init(const char* model_path);

BABYLON_EXPORT void babylon_kokoro_tts(const char* text, const char* voice_path, float speed, const char* output_path);

BABYLON_EXPORT void babylon_kokoro_free(void);

#ifdef __cplusplus
}

// Text normalization (numbers, ordinals, abbreviations)
std::string normalize_text(const std::string& text);

// Split UTF-8 string into individual unicode character strings
std::vector<std::string> utf8_chars(const std::string& s);

namespace OpenPhonemizer {

  // Encode a single word to input token IDs for the ONNX model (padded to 64)
  std::vector<int64_t> encode_word(const std::string& word);

  // Decode argmax logits [seq_len x vocab_size] to IPA phoneme string
  std::string decode_phonemes(const float* logits, int seq_len, int vocab_size);

  class Session {
    public:
      Session(
        const std::string& model_path,
        const std::string& dictionary_path = "",
        const bool use_punctuation = false
      );
      ~Session();

      // Returns concatenated IPA phoneme string for the full text
      std::string phonemize(const std::string& text);

      // Returns Kokoro-compatible token IDs
      std::vector<int64_t> phonemize_tokens(const std::string& text);

    private:
      bool use_punctuation;
      Ort::Env env;
      Ort::Session* session;
      std::unordered_map<std::string, std::string> dictionary;

      std::string phonemize_word(const std::string& word);
  };
}

namespace Vits {
  class SequenceTokenizer {
    public:
      SequenceTokenizer(const std::vector<std::string>& phonemes, const std::vector<int>& phoneme_ids);
      std::vector<int64_t> operator()(const std::vector<std::string>& phonemes) const;

    private:
      std::unordered_map<std::string, int> token_to_idx;
  };

  class Session {
    public:
      Session(const std::string& model_path);
      ~Session();

      void tts(const std::vector<std::string>& phonemes, const std::string& output_path);

    private:
      int sample_rate;
      std::vector<float> scales;

      Ort::Env env;
      Ort::Session* session;
      SequenceTokenizer* phoneme_tokenizer;
  };
}

namespace Kokoro {

  // Encode IPA phoneme string to Kokoro model token IDs
  // Returns vector wrapped with special token 0 on both ends
  std::vector<int64_t> encode_phonemes(const std::string& phonemes);

  class Session {
    public:
      Session(const std::string& model_path);
      ~Session();

      void tts(
        const std::string& phonemes,
        const std::string& voice_path,
        float speed,
        const std::string& output_path
      );

    private:
      static const int STYLE_DIM = 256;
      static const int MAX_PHONEME_LENGTH = 510;
      static const int SAMPLE_RATE = 24000;

      Ort::Env env;
      Ort::Session* session;

      std::vector<float> load_voice_style(const std::string& voice_path, int n_tokens);
  };
}

#endif

#endif // BABYLON_H
