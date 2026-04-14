from __future__ import annotations

import ctypes
import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Union


PathLike = Union[str, os.PathLike[str]]
CURRENT_DIR = Path(__file__).resolve().parent


class BabylonError(RuntimeError):
    pass


@dataclass(frozen=True)
class TimingItem:
    token: str
    kind: str
    start_sample: int
    end_sample: int
    duration_samples: int
    duration_units: int
    start_seconds: float
    end_seconds: float
    duration_seconds: float

    def to_dict(self) -> dict:
        return {
            "token": self.token,
            "kind": self.kind,
            "start_sample": self.start_sample,
            "end_sample": self.end_sample,
            "duration_samples": self.duration_samples,
            "duration_units": self.duration_units,
            "start_seconds": self.start_seconds,
            "end_seconds": self.end_seconds,
            "duration_seconds": self.duration_seconds,
        }


@dataclass(frozen=True)
class TimingTrace:
    sample_rate: int
    samples_per_unit: int
    audio_samples: int
    items: list[TimingItem]

    @property
    def duration_seconds(self) -> float:
        if self.sample_rate <= 0:
            return 0.0
        return self.audio_samples / float(self.sample_rate)

    def to_dict(self) -> dict:
        return {
            "sample_rate": self.sample_rate,
            "samples_per_unit": self.samples_per_unit,
            "audio_samples": self.audio_samples,
            "duration_seconds": self.duration_seconds,
            "items": [item.to_dict() for item in self.items],
        }


class _BabylonG2POptions(ctypes.Structure):
    _fields_ = [
        ("dictionary_path", ctypes.c_char_p),
        ("use_punctuation", ctypes.c_ubyte),
    ]


class _BabylonTimingItem(ctypes.Structure):
    _fields_ = [
        ("token", ctypes.c_char_p),
        ("kind", ctypes.c_char_p),
        ("start_sample", ctypes.c_longlong),
        ("end_sample", ctypes.c_longlong),
        ("duration_samples", ctypes.c_longlong),
        ("duration_units", ctypes.c_longlong),
        ("start_seconds", ctypes.c_double),
        ("end_seconds", ctypes.c_double),
        ("duration_seconds", ctypes.c_double),
    ]


class _BabylonTimingResult(ctypes.Structure):
    _fields_ = [
        ("sample_rate", ctypes.c_int),
        ("samples_per_unit", ctypes.c_longlong),
        ("audio_samples", ctypes.c_longlong),
        ("count", ctypes.c_longlong),
        ("items", ctypes.POINTER(_BabylonTimingItem)),
    ]


def _platform_library_name() -> tuple[str, str]:
    if sys.platform.startswith("win"):
        return "libbabylon.dll", "windows"
    if sys.platform == "darwin":
        return "libbabylon.dylib", "macos"
    return "libbabylon.so", "linux"


def _iter_library_candidates():
    library_name, platform_dir = _platform_library_name()
    roots = [CURRENT_DIR, CURRENT_DIR.parent]
    rel_paths = [
        Path(platform_dir) / library_name,
        Path(platform_dir) / "bin" / library_name,
        Path("bin") / library_name,
        Path(library_name),
    ]

    seen = set()
    for root in roots:
        for rel_path in rel_paths:
            candidate = (root / rel_path).resolve()
            if candidate in seen:
                continue
            seen.add(candidate)
            yield candidate


def _load_library() -> ctypes.CDLL:
    errors = []
    for candidate in _iter_library_candidates():
        if not candidate.exists():
            continue
        try:
            if sys.platform.startswith("win") and hasattr(os, "add_dll_directory"):
                os.add_dll_directory(str(candidate.parent))
            return ctypes.CDLL(str(candidate))
        except OSError as exc:
            errors.append(f"{candidate}: {exc}")

    searched = "\n".join(f"  - {candidate}" for candidate in _iter_library_candidates())
    detail = "\n".join(errors) if errors else "  (no candidate files found)"
    raise OSError(
        "Could not load libbabylon.\n"
        f"Searched:\n{searched}\n"
        f"Errors:\n{detail}"
    )


def _encode_path(value: PathLike) -> bytes:
    return os.fspath(value).encode("utf-8")


def _encode_optional_path(value: Optional[PathLike]) -> Optional[bytes]:
    if value is None:
        return None
    return _encode_path(value)


def _decode_c_string(value) -> str:
    if not value:
        return ""
    return value.decode("utf-8")


def _require_pointer(ptr, name: str):
    if ptr:
        return ptr
    raise BabylonError(f"{name} failed.")


def _convert_timing_result(result_ptr) -> TimingTrace:
    result_ptr = _require_pointer(result_ptr, "timing request")
    try:
        result = result_ptr.contents
        items: list[TimingItem] = []
        for index in range(result.count):
            item = result.items[index]
            items.append(
                TimingItem(
                    token=_decode_c_string(item.token),
                    kind=_decode_c_string(item.kind),
                    start_sample=int(item.start_sample),
                    end_sample=int(item.end_sample),
                    duration_samples=int(item.duration_samples),
                    duration_units=int(item.duration_units),
                    start_seconds=float(item.start_seconds),
                    end_seconds=float(item.end_seconds),
                    duration_seconds=float(item.duration_seconds),
                )
            )

        return TimingTrace(
            sample_rate=int(result.sample_rate),
            samples_per_unit=int(result.samples_per_unit),
            audio_samples=int(result.audio_samples),
            items=items,
        )
    finally:
        babylon_lib.babylon_timing_result_free(result_ptr)


babylon_lib = _load_library()

babylon_lib.babylon_g2p_init.argtypes = [ctypes.c_char_p, _BabylonG2POptions]
babylon_lib.babylon_g2p_init.restype = ctypes.c_int

babylon_lib.babylon_g2p.argtypes = [ctypes.c_char_p]
babylon_lib.babylon_g2p.restype = ctypes.c_char_p

babylon_lib.babylon_g2p_tokens.argtypes = [ctypes.c_char_p]
babylon_lib.babylon_g2p_tokens.restype = ctypes.POINTER(ctypes.c_int)

babylon_lib.babylon_g2p_free.argtypes = []
babylon_lib.babylon_g2p_free.restype = None

babylon_lib.babylon_tts_init.argtypes = [ctypes.c_char_p]
babylon_lib.babylon_tts_init.restype = ctypes.c_int

babylon_lib.babylon_tts.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
babylon_lib.babylon_tts.restype = None

babylon_lib.babylon_tts_with_timings.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
babylon_lib.babylon_tts_with_timings.restype = ctypes.POINTER(_BabylonTimingResult)

babylon_lib.babylon_tts_timings.argtypes = [ctypes.c_char_p]
babylon_lib.babylon_tts_timings.restype = ctypes.POINTER(_BabylonTimingResult)

babylon_lib.babylon_tts_free.argtypes = []
babylon_lib.babylon_tts_free.restype = None

babylon_lib.babylon_kokoro_init.argtypes = [ctypes.c_char_p]
babylon_lib.babylon_kokoro_init.restype = ctypes.c_int

babylon_lib.babylon_kokoro_tts.argtypes = [
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.c_float,
    ctypes.c_char_p,
]
babylon_lib.babylon_kokoro_tts.restype = None

babylon_lib.babylon_kokoro_tts_with_timings.argtypes = [
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.c_float,
    ctypes.c_char_p,
]
babylon_lib.babylon_kokoro_tts_with_timings.restype = ctypes.POINTER(_BabylonTimingResult)

babylon_lib.babylon_kokoro_timings.argtypes = [
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.c_float,
]
babylon_lib.babylon_kokoro_timings.restype = ctypes.POINTER(_BabylonTimingResult)

babylon_lib.babylon_kokoro_free.argtypes = []
babylon_lib.babylon_kokoro_free.restype = None

babylon_lib.babylon_kitten_init.argtypes = [ctypes.c_char_p]
babylon_lib.babylon_kitten_init.restype = ctypes.c_int

babylon_lib.babylon_kitten_tts.argtypes = [
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.c_float,
    ctypes.c_char_p,
]
babylon_lib.babylon_kitten_tts.restype = None

babylon_lib.babylon_kitten_tts_with_timings.argtypes = [
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.c_float,
    ctypes.c_char_p,
]
babylon_lib.babylon_kitten_tts_with_timings.restype = ctypes.POINTER(_BabylonTimingResult)

babylon_lib.babylon_kitten_timings.argtypes = [
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.c_float,
]
babylon_lib.babylon_kitten_timings.restype = ctypes.POINTER(_BabylonTimingResult)

babylon_lib.babylon_kitten_free.argtypes = []
babylon_lib.babylon_kitten_free.restype = None

babylon_lib.babylon_timing_result_free.argtypes = [ctypes.POINTER(_BabylonTimingResult)]
babylon_lib.babylon_timing_result_free.restype = None


def g2p_init(
    model_path: PathLike,
    dictionary_path: Optional[PathLike] = None,
    use_punctuation: bool = True,
) -> int:
    options = _BabylonG2POptions(
        dictionary_path=_encode_optional_path(dictionary_path),
        use_punctuation=1 if use_punctuation else 0,
    )
    return babylon_lib.babylon_g2p_init(_encode_path(model_path), options)


def g2p(text: str) -> str:
    result = _require_pointer(babylon_lib.babylon_g2p(text.encode("utf-8")), "g2p")
    return _decode_c_string(result)


def g2p_tokens(text: str) -> list[int]:
    result_ptr = _require_pointer(babylon_lib.babylon_g2p_tokens(text.encode("utf-8")), "g2p_tokens")
    tokens = []
    index = 0
    while result_ptr[index] != -1:
        tokens.append(int(result_ptr[index]))
        index += 1
    return tokens


def g2p_free() -> None:
    babylon_lib.babylon_g2p_free()


def tts_init(model_path: PathLike) -> int:
    return babylon_lib.babylon_tts_init(_encode_path(model_path))


def tts(text: str, output_path: PathLike) -> None:
    babylon_lib.babylon_tts(text.encode("utf-8"), _encode_path(output_path))


def tts_with_timings(text: str, output_path: PathLike) -> TimingTrace:
    return _convert_timing_result(
        babylon_lib.babylon_tts_with_timings(text.encode("utf-8"), _encode_path(output_path))
    )


def tts_timings(text: str) -> TimingTrace:
    return _convert_timing_result(
        babylon_lib.babylon_tts_timings(text.encode("utf-8"))
    )


def tts_free() -> None:
    babylon_lib.babylon_tts_free()


def kokoro_init(model_path: PathLike) -> int:
    return babylon_lib.babylon_kokoro_init(_encode_path(model_path))


def kokoro_tts(
    text: str,
    voice_path: PathLike,
    speed: float = 1.0,
    output_path: PathLike = "output.wav",
) -> None:
    babylon_lib.babylon_kokoro_tts(
        text.encode("utf-8"),
        _encode_path(voice_path),
        float(speed),
        _encode_path(output_path),
    )


def kokoro_tts_with_timings(
    text: str,
    voice_path: PathLike,
    speed: float = 1.0,
    output_path: PathLike = "output.wav",
) -> TimingTrace:
    return _convert_timing_result(
        babylon_lib.babylon_kokoro_tts_with_timings(
            text.encode("utf-8"),
            _encode_path(voice_path),
            float(speed),
            _encode_path(output_path),
        )
    )


def kokoro_timings(
    text: str,
    voice_path: PathLike,
    speed: float = 1.0,
) -> TimingTrace:
    return _convert_timing_result(
        babylon_lib.babylon_kokoro_timings(
            text.encode("utf-8"),
            _encode_path(voice_path),
            float(speed),
        )
    )


def kokoro_free() -> None:
    babylon_lib.babylon_kokoro_free()


def kitten_init(model_path: PathLike) -> int:
    return babylon_lib.babylon_kitten_init(_encode_path(model_path))


def kitten_tts(
    text: str,
    voice_path: PathLike,
    speed: float = 1.0,
    output_path: PathLike = "output.wav",
) -> None:
    babylon_lib.babylon_kitten_tts(
        text.encode("utf-8"),
        _encode_path(voice_path),
        float(speed),
        _encode_path(output_path),
    )


def kitten_tts_with_timings(
    text: str,
    voice_path: PathLike,
    speed: float = 1.0,
    output_path: PathLike = "output.wav",
) -> TimingTrace:
    return _convert_timing_result(
        babylon_lib.babylon_kitten_tts_with_timings(
            text.encode("utf-8"),
            _encode_path(voice_path),
            float(speed),
            _encode_path(output_path),
        )
    )


def kitten_timings(
    text: str,
    voice_path: PathLike,
    speed: float = 1.0,
) -> TimingTrace:
    return _convert_timing_result(
        babylon_lib.babylon_kitten_timings(
            text.encode("utf-8"),
            _encode_path(voice_path),
            float(speed),
        )
    )


def kitten_free() -> None:
    babylon_lib.babylon_kitten_free()


# Backwards-compatible aliases for older wrapper naming.
init_g2p = g2p_init
init_tts = tts_init
free_g2p = g2p_free
free_tts = tts_free
init_kokoro = kokoro_init
free_kokoro = kokoro_free
init_kitten = kitten_init
free_kitten = kitten_free
vits_init = tts_init
vits_tts = tts
vits_tts_with_timings = tts_with_timings
vits_timings = tts_timings
vits_free = tts_free


__all__ = [
    "BabylonError",
    "TimingItem",
    "TimingTrace",
    "babylon_lib",
    "g2p_init",
    "g2p",
    "g2p_tokens",
    "g2p_free",
    "tts_init",
    "tts",
    "tts_with_timings",
    "tts_timings",
    "tts_free",
    "kokoro_init",
    "kokoro_tts",
    "kokoro_tts_with_timings",
    "kokoro_timings",
    "kokoro_free",
    "kitten_init",
    "kitten_tts",
    "kitten_tts_with_timings",
    "kitten_timings",
    "kitten_free",
    "init_g2p",
    "init_tts",
    "free_g2p",
    "free_tts",
    "init_kokoro",
    "free_kokoro",
    "init_kitten",
    "free_kitten",
    "vits_init",
    "vits_tts",
    "vits_tts_with_timings",
    "vits_timings",
    "vits_free",
]


if __name__ == "__main__":
    repo_root = CURRENT_DIR.parent
    g2p_model_path = repo_root / "models" / "open-phonemizer.onnx"
    dictionary_path = repo_root / "data" / "dictionary.json"
    vits_model_path = repo_root / "models" / "en-US-curie-vits.onnx"
    kokoro_model_path = repo_root / "models" / "kokoro-quantized.onnx"
    kitten_model_path = repo_root / "models" / "kitten-tts.onnx"
    voice_path = repo_root / "models" / "kokoro-voices" / "en-US-heart.bin"
    kitten_voice_path = repo_root / "models" / "kitten-voices" / "en-US-bella.bin"
    sequence = "Hello world, this is a python timing test of Babylon."

    if g2p_init(g2p_model_path, dictionary_path) == 0:
        print("G2P initialized successfully")
        print("Phonemes:", g2p(sequence))
        print("Tokens:", g2p_tokens(sequence))
    else:
        print("Failed to initialize G2P")

    if tts_init(vits_model_path) == 0:
        print("VITS initialized successfully")
        trace = tts_with_timings(sequence, repo_root / "output-vits.wav")
        print("VITS duration:", trace.duration_seconds)
        print("VITS first items:", trace.items[:5])
    else:
        print("Failed to initialize VITS")

    if kokoro_init(kokoro_model_path) == 0:
        print("Kokoro initialized successfully")
        trace = kokoro_tts_with_timings(sequence, voice_path, speed=1.0, output_path=repo_root / "output-kokoro.wav")
        print("Kokoro duration:", trace.duration_seconds)
        print("Kokoro first items:", trace.items[:5])
    else:
        print("Failed to initialize Kokoro")

    if kitten_init(kitten_model_path) == 0:
        print("Kitten initialized successfully")
        trace = kitten_tts_with_timings(sequence, kitten_voice_path, speed=1.0, output_path=repo_root / "output-kitten.wav")
        print("Kitten duration:", trace.duration_seconds)
        print("Kitten first items:", trace.items[:5])
    else:
        print("Failed to initialize Kitten")

    kitten_free()
    kokoro_free()
    tts_free()
    g2p_free()
