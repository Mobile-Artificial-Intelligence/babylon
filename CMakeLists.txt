cmake_minimum_required(VERSION 3.18)

project(
    babylon
    VERSION 0.0.2
    DESCRIPTION "Babylon Neural Text To Speech Engine"
    HOMEPAGE_URL "https://github.com/danemadsen/babylon.cpp"
    LANGUAGES CXX
)

set(BABYLON_BIN_DIR ${CMAKE_CURRENT_LIST_DIR}/bin CACHE PATH "Babylon output directory")

# Enable C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set compiler options to support UTF-8 encoding
if(MSVC)
    add_compile_options("/utf-8")
    # Use dynamic CRT (/MD) to match onnxruntime and avoid multiply-defined symbol errors
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "" FORCE)
else()
    add_compile_options("-finput-charset=UTF-8" "-fexec-charset=UTF-8")
endif()

# Add the babylon library
add_library(
    babylon
    SHARED
    src/babylon.cpp
    src/cleaners.cpp
    src/phonemizer.cpp
    src/voice.cpp
)

if(NOT APPLE)
    target_link_options(babylon PRIVATE LINKER:--build-id=none)
    target_compile_options(babylon PRIVATE "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=babylon")
endif()

# Output the shared library alongside the CLI binary in bin/
set_target_properties(
    babylon
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${BABYLON_BIN_DIR}
)

# On Linux/non-Apple: look for sibling shared libs in the same directory
if(NOT APPLE AND NOT ANDROID)
    set_target_properties(
        babylon
        PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# ── Build ONNX Runtime from source (submodules/onnxruntime) ──────────────────

include(ExternalProject)

set(ONNXRUNTIME_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/submodules/onnxruntime/cmake")
set(ONNXRUNTIME_INSTALL_DIR "${CMAKE_BINARY_DIR}/onnxruntime-install")

# Collect extra args for cross-compilation (Android, toolchain, etc.)
set(ONNXRUNTIME_EXTRA_CMAKE_ARGS "")

if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND ONNXRUNTIME_EXTRA_CMAKE_ARGS
        "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
    )
endif()

if(ANDROID)
    list(APPEND ONNXRUNTIME_EXTRA_CMAKE_ARGS
        "-DANDROID_ABI=${ANDROID_ABI}"
        "-DANDROID_PLATFORM=${ANDROID_PLATFORM}"
        "-DANDROID_STL=c++_shared"
    )
endif()

if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(APPEND ONNXRUNTIME_EXTRA_CMAKE_ARGS
        "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}"
    )
endif()

if(WIN32)
    # Force dynamic CRT (/MD) to avoid LNK4098 / multiply-defined symbol errors
    list(APPEND ONNXRUNTIME_EXTRA_CMAKE_ARGS
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"
    )
    set(ONNXRUNTIME_BYPRODUCTS
        "${ONNXRUNTIME_INSTALL_DIR}/lib/onnxruntime.lib"
        "${ONNXRUNTIME_INSTALL_DIR}/bin/onnxruntime.dll"
    )
elseif(APPLE)
    set(ONNXRUNTIME_BYPRODUCTS
        "${ONNXRUNTIME_INSTALL_DIR}/lib/libonnxruntime.dylib"
    )
else()
    set(ONNXRUNTIME_BYPRODUCTS
        "${ONNXRUNTIME_INSTALL_DIR}/lib/libonnxruntime.so"
    )
endif()

ExternalProject_Add(
    onnxruntime_build
    SOURCE_DIR   "${ONNXRUNTIME_SOURCE_DIR}"
    BINARY_DIR   "${CMAKE_BINARY_DIR}/onnxruntime-build"
    INSTALL_DIR  "${ONNXRUNTIME_INSTALL_DIR}"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -Donnxruntime_BUILD_SHARED_LIB=ON
        -Donnxruntime_BUILD_UNIT_TESTS=OFF
        -Donnxruntime_BUILD_CSHARP=OFF
        -Donnxruntime_ENABLE_PYTHON=OFF
        -Donnxruntime_USE_FULL_PROTOBUF=OFF
        # Prevent FetchContent from finding Homebrew/system packages (e.g. re2→absl)
        # which conflict with onnxruntime's own source builds of those deps.
        -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER
        ${ONNXRUNTIME_EXTRA_CMAKE_ARGS}
    BUILD_BYPRODUCTS
        ${ONNXRUNTIME_BYPRODUCTS}
)

# Platform-specific library paths after install
if(WIN32)
    set(ONNXRUNTIME_LIB     "${ONNXRUNTIME_INSTALL_DIR}/lib/onnxruntime.lib")
    set(ONNXRUNTIME_RUNTIME "${ONNXRUNTIME_INSTALL_DIR}/bin/onnxruntime.dll")
elseif(APPLE)
    set(ONNXRUNTIME_LIB "${ONNXRUNTIME_INSTALL_DIR}/lib/libonnxruntime.dylib")
else()
    set(ONNXRUNTIME_LIB "${ONNXRUNTIME_INSTALL_DIR}/lib/libonnxruntime.so")
endif()

# Copy the onnxruntime shared library into bin/ after it is built
if(WIN32)
    add_custom_command(
        OUTPUT  "${BABYLON_BIN_DIR}/onnxruntime.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_RUNTIME}"
                "${BABYLON_BIN_DIR}/onnxruntime.dll"
        DEPENDS onnxruntime_build
        COMMENT "Copying onnxruntime.dll to bin/"
    )
    add_custom_target(copy_onnxruntime ALL
        DEPENDS "${BABYLON_BIN_DIR}/onnxruntime.dll"
    )
elseif(APPLE)
    add_custom_command(
        OUTPUT  "${BABYLON_BIN_DIR}/libonnxruntime.dylib"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_LIB}"
                "${BABYLON_BIN_DIR}/libonnxruntime.dylib"
        DEPENDS onnxruntime_build
        COMMENT "Copying libonnxruntime.dylib to bin/"
    )
    add_custom_target(copy_onnxruntime ALL
        DEPENDS "${BABYLON_BIN_DIR}/libonnxruntime.dylib"
    )
else()
    add_custom_command(
        OUTPUT  "${BABYLON_BIN_DIR}/libonnxruntime.so"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_LIB}"
                "${BABYLON_BIN_DIR}/libonnxruntime.so"
        DEPENDS onnxruntime_build
        COMMENT "Copying libonnxruntime.so to bin/"
    )
    add_custom_target(copy_onnxruntime ALL
        DEPENDS "${BABYLON_BIN_DIR}/libonnxruntime.so"
    )
endif()

# Copy models/ into bin/models/ — uses a stamp so large files are only copied once.
# Delete bin/.models_stamp to force a re-copy.
# Skipped silently if the models/ directory doesn't exist (e.g. in CI).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models")
    add_custom_command(
        OUTPUT  "${BABYLON_BIN_DIR}/.models_stamp"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/models"
                "${BABYLON_BIN_DIR}/models"
        COMMAND ${CMAKE_COMMAND} -E touch "${BABYLON_BIN_DIR}/.models_stamp"
        COMMENT "Copying models/ to bin/models/"
    )
    add_custom_target(copy_models ALL
        DEPENDS "${BABYLON_BIN_DIR}/.models_stamp"
    )
endif()

# Copy cli/config.json into bin/ at every build (small file, always up to date)
add_custom_target(copy_config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/cli/config.json"
            "${BABYLON_BIN_DIR}/config.json"
    COMMENT "Copying config.json to bin/"
)

# Declare an imported target backed by the installed library
add_library(onnxruntime SHARED IMPORTED)
add_dependencies(onnxruntime onnxruntime_build)

if(WIN32)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_IMPLIB   "${ONNXRUNTIME_LIB}"
        IMPORTED_LOCATION "${ONNXRUNTIME_RUNTIME}"
    )
else()
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_LIB}"
    )
endif()

# Include ONNX Runtime headers
target_include_directories(
    babylon
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    "${ONNXRUNTIME_INSTALL_DIR}/include/onnxruntime"
)

add_dependencies(babylon onnxruntime_build copy_onnxruntime)

target_link_directories(
    babylon PUBLIC
    ${BABYLON_BIN_DIR}
)

if(WIN32)
    target_link_libraries(
        babylon
        onnxruntime.lib
    )
elseif(APPLE)
    target_link_libraries(
        babylon
        onnxruntime
        "-framework CoreFoundation"
        "-framework Foundation"
    )
else()
    target_link_libraries(
        babylon
        onnxruntime
    )
endif()

# CLI
option(BUILD_CLI "Build babylon CLI" OFF)

if(BUILD_CLI)
    add_subdirectory(cli)
endif()
