#ifndef BABYLON_PATH_UTILS_H
#define BABYLON_PATH_UTILS_H

#include "babylon.h"

#include <filesystem>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace BabylonPath {

using OrtPathString = std::basic_string<ORTCHAR_T>;

#ifdef _WIN32
inline std::wstring multi_byte_to_wide(UINT code_page, DWORD flags, const std::string& path) {
    int length = MultiByteToWideChar(code_page, flags, path.c_str(), -1, nullptr, 0);
    if (length <= 0) {
        return {};
    }

    std::wstring wide_path(static_cast<size_t>(length), L'\0');
    int written = MultiByteToWideChar(code_page, flags, path.c_str(), -1, wide_path.data(), length);
    if (written <= 0) {
        return {};
    }

    wide_path.resize(static_cast<size_t>(written - 1));
    return wide_path;
}

inline std::wstring wide_path(const std::string& path) {
    if (path.empty()) {
        return {};
    }

    std::wstring converted = multi_byte_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, path);
    if (!converted.empty()) {
        return converted;
    }

    converted = multi_byte_to_wide(CP_ACP, 0, path);
    if (!converted.empty()) {
        return converted;
    }

    throw std::runtime_error("Failed to convert Windows path to wide characters.");
}
#endif

inline OrtPathString ort_path(const std::string& path) {
#ifdef _WIN32
    return wide_path(path);
#else
    return path;
#endif
}

inline OrtPathString ort_path(const std::filesystem::path& path) {
#ifdef _WIN32
    return path.native();
#else
    return path.string();
#endif
}

inline std::filesystem::path filesystem_path(const std::string& path) {
#ifdef _WIN32
    return std::filesystem::path(wide_path(path));
#else
    return std::filesystem::path(path);
#endif
}

} // namespace BabylonPath

#endif // BABYLON_PATH_UTILS_H
