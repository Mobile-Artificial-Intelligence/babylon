<div align="center">
  <img alt="logo" height="200px" src="babylon.svg">
</div>

# babylon.cpp

[![Build Windows](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-windows.yml/badge.svg)](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-windows.yml)
[![Build MacOS](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-macos.yml/badge.svg)](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-macos.yml)
[![Build Linux](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-linux.yml/badge.svg)](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-linux.yml)
[![Build Android](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-android.yml/badge.svg)](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-android.yml)

Babylon.cpp is a C and C++ library for grapheme-to-phoneme (G2P) conversion and neural text-to-speech (TTS) synthesis. All inference runs locally using [ONNX Runtime](https://github.com/microsoft/onnxruntime) — no internet connection is required and no data leaves the host machine.

It supports two TTS engines:

- **[Kokoro](https://github.com/hexgrad/kokoro)** — High-quality multi-voice neural TTS at 24 kHz with 54+ voices across multiple languages.
- **[VITS](https://github.com/jaywalnut310/vits)** — End-to-end neural TTS; compatible with [Piper](https://github.com/rhasspy/piper) models.

Phonemization is handled by **[Open Phonemizer](https://github.com/NeuralVox/OpenPhonemizer)** backed by a ~130,000-entry pronunciation dictionary.

## Platforms

| Platform | Architecture           | Library              |
|----------|------------------------|----------------------|
| Linux    | x86_64                 | `libbabylon.so`      |
| macOS    | Universal (x86_64 + arm64) | `libbabylon.dylib` |
| Windows  | x86_64                 | `babylon.dll`        |
| Android  | arm64-v8a, x86_64      | `libbabylon.so`      |

## Building

Requires CMake 3.18+, a C++17 compiler, and Git.

```bash
git clone --recursive https://github.com/Mobile-Artificial-Intelligence/babylon.cpp.git
cd babylon.cpp
make cli
```

This builds the library, CLI binary, and ONNX Runtime from source. All output goes to `bin/`.

| Target         | Description                              |
|----------------|------------------------------------------|
| `make lib`     | Library only                             |
| `make cli`     | Library + CLI binary + runtime files     |
| `make debug`   | CLI build in Debug mode                  |
| `make android` | Cross-compile for Android (requires NDK) |

## CLI

The `babylon` binary provides three subcommands. It auto-loads `config.json` from the same directory as the executable on startup.

```bash
# Phonemize text to IPA
babylon phonemize "Hello world"
# → hɛloʊ wɜːld

# Synthesise speech (Kokoro, default)
babylon tts "Hello world" -o hello.wav
babylon tts --voice en-US-nova --speed 1.2 "Hello world"

# Synthesise speech (VITS)
babylon tts --vits "Hello world" -o hello.wav

# Start the REST API server and web frontend
babylon serve
babylon serve --host 0.0.0.0 --port 9000
```

Global flags (apply to all subcommands):

```
--config <path>           Load a JSON config file
--phonemizer-model <path> Phonemizer ONNX model
--dictionary <path>       Pronunciation dictionary JSON
--kokoro-model <path>     Kokoro ONNX model
--kokoro-voice <name>     Default Kokoro voice
--kokoro-voices <dir>     Directory of voice .bin files
--vits-model <path>       VITS ONNX model
```

## REST API

When running `babylon serve`, the following endpoints are available:

| Method | Path         | Description                            |
|--------|--------------|----------------------------------------|
| GET    | `/`          | Web frontend (HTML)                    |
| GET    | `/status`    | Engine availability and voice count    |
| GET    | `/voices`    | List of available Kokoro voice names   |
| POST   | `/phonemize` | Convert text to IPA or token IDs       |
| POST   | `/tts`       | Synthesise speech, returns `audio/wav` |

**POST /tts** body:
```json
{
  "text":   "Hello world",
  "engine": "kokoro",
  "voice":  "en-US-heart",
  "speed":  1.0
}
```

**POST /phonemize** body:
```json
{ "text": "Hello world", "tokens": false }
```

## C API

```c
#include "babylon.h"

int main(void) {
    babylon_g2p_options_t opts = {
        .dictionary_path = "models/dictionary.json",
        .use_punctuation = 1,
    };

    babylon_g2p_init("models/open-phonemizer.onnx", opts);
    babylon_kokoro_init("models/kokoro-quantized.onnx");

    babylon_kokoro_tts(
        "Hello world",
        "models/voices/en-US-heart.bin",
        1.0f,
        "output.wav"
    );

    babylon_kokoro_free();
    babylon_g2p_free();
    return 0;
}
```

## C++ API

```cpp
#include "babylon.h"

int main() {
    OpenPhonemizer::Session phonemizer(
        "models/open-phonemizer.onnx",
        "models/dictionary.json",
        /* use_punctuation = */ true
    );

    Kokoro::Session kokoro("models/kokoro-quantized.onnx");

    std::string phonemes = phonemizer.phonemize("Hello world");
    kokoro.tts(phonemes, "models/voices/en-US-heart.bin", 1.0f, "output.wav");

    return 0;
}
```

## Documentation

A full manual is available in [`docs/manual.tex`](docs/manual.tex), covering the complete C and C++ API reference, CLI options, REST API, build instructions, and model configuration.
