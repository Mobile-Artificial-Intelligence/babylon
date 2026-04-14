<div align="center">
  <img alt="logo" height="200px" src="babylon.svg">
</div>

# babylon.cpp

[![Build Windows](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-windows.yml/badge.svg)](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-windows.yml)
[![Build MacOS](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-macos.yml/badge.svg)](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-macos.yml)
[![Build Linux](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-linux.yml/badge.svg)](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-linux.yml)
[![Build Android](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-android.yml/badge.svg)](https://github.com/Mobile-Artificial-Intelligence/babylon.cpp/actions/workflows/build-android.yml)

Babylon.cpp is a C and C++ library for grapheme-to-phoneme (G2P) conversion and neural text-to-speech (TTS) synthesis. All inference runs locally using [ONNX Runtime](https://github.com/microsoft/onnxruntime) — no internet connection is required and no data leaves the host machine.

It supports three TTS engines:

- **[Kokoro](https://github.com/hexgrad/kokoro)** — High-quality multi-voice neural TTS at 24 kHz with 54+ voices across multiple languages.
- **kittenTTS** — A lightweight 24 kHz multi-voice model that reuses the Open Phonemizer/Kokoro phoneme pipeline with engine-specific voice embeddings.
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

This builds the library, CLI binary, and ONNX Runtime from source. Both the source-root `bin/` output and `cmake --install` use the same runtime layout:

- `bin/babylon` — CLI executable
- `bin/lib/` — shared libraries
- `bin/data/` — config, dictionary, web UI, and viseme assets
- `bin/models/` — ONNX model files
- `bin/voices/` — Kokoro and kittenTTS voice files

To install into a separate prefix:

```bash
make install INSTALL_DIR=/path/to/stage
# or
cmake --install build --prefix /path/to/stage
```

| Target         | Description                              |
|----------------|------------------------------------------|
| `make lib`     | Library only                             |
| `make cli`     | Library + CLI binary + runtime files     |
| `make install` | Install the same layout into `INSTALL_DIR` |
| `make debug`   | CLI build in Debug mode                  |
| `make android` | Cross-compile for Android (requires NDK) |

## CLI

The `babylon` binary provides three subcommands. It auto-loads `data/config.json` from the installed tree on startup.

```bash
# Phonemize text to IPA
babylon phonemize "Hello world"
# → hɛloʊ wɜːld

# Synthesise speech (Kokoro, default)
babylon tts "Hello world" -o hello.wav
babylon tts --voice en-US-nova --speed 1.2 "Hello world"

# Synthesise speech (kittenTTS)
babylon tts --kitten --voice en-US-bella "Hello world" -o hello.wav

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
--kitten-model <path>     kittenTTS ONNX model
--kitten-voice <name>     Default kittenTTS voice
--kitten-voices <dir>     Directory of kittenTTS voice .bin files
--vits-model <path>       VITS ONNX model
```

## REST API

When running `babylon serve`, the following endpoints are available:

| Method | Path         | Description                            |
|--------|--------------|----------------------------------------|
| GET    | `/`          | Web frontend (HTML)                    |
| GET    | `/status`    | Engine availability and voice count    |
| GET    | `/voices`    | List of available Kokoro voice names   |
| GET    | `/voices/kitten` | List of available kittenTTS voice names |
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

`engine` accepts `kokoro`, `kitten`, or `vits`. Voice selection applies to the Kokoro and kittenTTS engines.

**POST /phonemize** body:
```json
{ "text": "Hello world", "tokens": false }
```

## C API

The C API exposes parallel entry points for each engine: `babylon_kokoro_*`, `babylon_kitten_*`, and the legacy `babylon_tts_*` VITS functions.

```c
#include "babylon.h"

int main(void) {
    babylon_g2p_options_t opts = {
        .dictionary_path = "data/dictionary.json",
        .use_punctuation = 1,
    };

    babylon_g2p_init("models/open-phonemizer.onnx", opts);
    babylon_kokoro_init("models/kokoro-quantized.onnx");

    babylon_kokoro_tts(
        "Hello world",
        "voices/kokoro/en-US-heart.bin",
        1.0f,
        "output.wav"
    );

    babylon_kokoro_free();
    babylon_g2p_free();
    return 0;
}
```

## C++ API

The C++ API follows the same split with `Kokoro::Session`, `Kitten::Session`, and `Vits::Session`.

```cpp
#include "babylon.h"

int main() {
    OpenPhonemizer::Session phonemizer(
        "models/open-phonemizer.onnx",
        "data/dictionary.json",
        /* use_punctuation = */ true
    );

    Kokoro::Session kokoro("models/kokoro-quantized.onnx");

    std::string phonemes = phonemizer.phonemize("Hello world");
    kokoro.tts(phonemes, "voices/kokoro/en-US-heart.bin", 1.0f, "output.wav");

    return 0;
}
```

## Documentation

A full manual is available in [`docs/manual.tex`](docs/manual.tex), covering the complete C and C++ API reference, CLI options, REST API, build instructions, and model configuration.
