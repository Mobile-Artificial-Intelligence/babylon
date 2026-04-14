cmake_minimum_required(VERSION 3.18)

project(
    babylon
    VERSION 1.0.0
    DESCRIPTION "Babylon Neural Text To Speech Engine"
    HOMEPAGE_URL "https://github.com/danemadsen/babylon.cpp"
    LANGUAGES CXX
)

set(BABYLON_BIN_DIR ${CMAKE_CURRENT_LIST_DIR}/bin CACHE PATH "Babylon executable output directory")
set(BABYLON_LIB_DIR ${BABYLON_BIN_DIR}/lib CACHE PATH "Babylon shared library output directory")
set(BABYLON_INSTALL_RUNTIME_DIR "bin" CACHE STRING "Babylon install directory for executables")
set(BABYLON_INSTALL_LIBRARY_DIR "bin/lib" CACHE STRING "Babylon install directory for shared libraries")
set(BABYLON_INSTALL_INCLUDE_DIR "include" CACHE STRING "Babylon install directory for public headers")

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
set(BABYLON_CORE_SOURCES
    src/lib/babylon.cpp
    src/lib/cleaners.cpp
    src/lib/phonemizer.cpp
    src/lib/voice.cpp
)

add_library(
    babylon
    SHARED
    ${BABYLON_CORE_SOURCES}
)

if(WIN32)
    add_library(
        babylon_static
        STATIC
        ${BABYLON_CORE_SOURCES}
    )
    set_target_properties(
        babylon_static
        PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${BABYLON_LIB_DIR}
    )
endif()

if(NOT APPLE AND NOT WIN32)
    target_link_options(babylon PRIVATE LINKER:--build-id=none)
    target_compile_options(babylon PRIVATE "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=babylon")
endif()

# Output shared libraries under bin/lib/
set_target_properties(
    babylon
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${BABYLON_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${BABYLON_LIB_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${BABYLON_LIB_DIR}
)

# Shared libraries resolve sibling dependencies from bin/lib on Unix-like platforms.
if(APPLE)
    set_target_properties(
        babylon
        PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@loader_path"
    )
elseif(UNIX AND NOT ANDROID)
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
    # Force the dynamic CRT (/MD) through ORT and its vendored deps.
    # ORT's protobuf/abseil/ONNX builds use their own MSVC runtime toggles,
    # so passing only CMAKE_MSVC_RUNTIME_LIBRARY is not enough here.
    list(APPEND ONNXRUNTIME_EXTRA_CMAKE_ARGS
        "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW"
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"
        "-Dprotobuf_MSVC_STATIC_RUNTIME=OFF"
        "-DABSL_MSVC_STATIC_RUNTIME=OFF"
        "-DONNX_USE_MSVC_STATIC_RUNTIME=OFF"
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

# Copy the onnxruntime shared library into bin/lib/ after it is built
if(WIN32)
    add_custom_command(
        OUTPUT  "${BABYLON_LIB_DIR}/onnxruntime.dll"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BABYLON_LIB_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_RUNTIME}"
                "${BABYLON_LIB_DIR}/onnxruntime.dll"
        DEPENDS onnxruntime_build
        COMMENT "Copying onnxruntime.dll to bin/lib/"
    )
    add_custom_target(copy_onnxruntime ALL
        DEPENDS "${BABYLON_LIB_DIR}/onnxruntime.dll"
    )
elseif(APPLE)
    # Copy all libonnxruntime*.dylib files preserving symlinks (-P).
    # ORT installs both a versioned file (libonnxruntime.1.23.0.dylib) and an
    # unversioned symlink (libonnxruntime.dylib). The versioned name is what is
    # embedded as the install name in the dylib and referenced by dependent
    # binaries, so both must be present in bin/.
    add_custom_command(
        OUTPUT  "${BABYLON_LIB_DIR}/libonnxruntime.dylib"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BABYLON_LIB_DIR}"
        COMMAND bash -c "cp -Pf '${ONNXRUNTIME_INSTALL_DIR}/lib/'libonnxruntime*.dylib '${BABYLON_LIB_DIR}/'"
        DEPENDS onnxruntime_build
        COMMENT "Copying libonnxruntime dylibs to bin/lib/"
    )
    add_custom_target(copy_onnxruntime ALL
        DEPENDS "${BABYLON_LIB_DIR}/libonnxruntime.dylib"
    )
else()
    add_custom_command(
        OUTPUT  "${BABYLON_LIB_DIR}/libonnxruntime.so"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BABYLON_LIB_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_LIB}"
                "${BABYLON_LIB_DIR}/libonnxruntime.so"
        DEPENDS onnxruntime_build
        COMMENT "Copying libonnxruntime.so to bin/lib/"
    )
    add_custom_target(copy_onnxruntime ALL
        DEPENDS "${BABYLON_LIB_DIR}/libonnxruntime.so"
    )
endif()

file(
    GLOB BABYLON_LEGACY_RUNTIME_LIBS
    LIST_DIRECTORIES false
    "${BABYLON_BIN_DIR}/babylon.dll"
    "${BABYLON_BIN_DIR}/libbabylon.*"
    "${BABYLON_BIN_DIR}/onnxruntime.dll"
    "${BABYLON_BIN_DIR}/libonnxruntime.so*"
    "${BABYLON_BIN_DIR}/libonnxruntime*.dylib"
)

# Copy install-time assets into bin/ while keeping shared libraries isolated in bin/lib/.
add_custom_target(clean_legacy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E rm -f
            ${BABYLON_LEGACY_RUNTIME_LIBS}
            "${BABYLON_BIN_DIR}/config.json"
            "${BABYLON_BIN_DIR}/index.html"
            "${BABYLON_BIN_DIR}/models/dictionary.json"
    COMMAND ${CMAKE_COMMAND} -E rm -rf
            "${BABYLON_BIN_DIR}/visemes"
            "${BABYLON_BIN_DIR}/models/kokoro-voices"
            "${BABYLON_BIN_DIR}/models/kitten-voices"
    COMMENT "Removing legacy asset paths from bin/"
)

file(
    GLOB_RECURSE BABYLON_MODEL_ASSETS
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/models/*"
)
file(
    GLOB_RECURSE BABYLON_VOICE_ASSETS
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/voices/*"
)
file(
    GLOB_RECURSE BABYLON_DATA_ASSETS
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/data/*"
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models")
    add_custom_command(
        OUTPUT  "${BABYLON_BIN_DIR}/.models_stamp"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/models"
                "${BABYLON_BIN_DIR}/models"
        COMMAND ${CMAKE_COMMAND} -E touch "${BABYLON_BIN_DIR}/.models_stamp"
        DEPENDS clean_legacy_assets ${BABYLON_MODEL_ASSETS}
        COMMENT "Copying models/ to bin/models/"
    )
    add_custom_target(copy_models ALL
        DEPENDS "${BABYLON_BIN_DIR}/.models_stamp"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/voices")
    add_custom_command(
        OUTPUT  "${BABYLON_BIN_DIR}/.voices_stamp"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/voices"
                "${BABYLON_BIN_DIR}/voices"
        COMMAND ${CMAKE_COMMAND} -E touch "${BABYLON_BIN_DIR}/.voices_stamp"
        DEPENDS clean_legacy_assets ${BABYLON_VOICE_ASSETS}
        COMMENT "Copying voices/ to bin/voices/"
    )
    add_custom_target(copy_voices ALL
        DEPENDS "${BABYLON_BIN_DIR}/.voices_stamp"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_custom_command(
        OUTPUT  "${BABYLON_BIN_DIR}/.data_stamp"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/data"
                "${BABYLON_BIN_DIR}/data"
        COMMAND ${CMAKE_COMMAND} -E touch "${BABYLON_BIN_DIR}/.data_stamp"
        DEPENDS clean_legacy_assets ${BABYLON_DATA_ASSETS}
        COMMENT "Copying data/ to bin/data/"
    )
    add_custom_target(copy_data ALL
        DEPENDS "${BABYLON_BIN_DIR}/.data_stamp"
    )
endif()

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
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/submodules/json/single_include
)

if(WIN32)
    target_include_directories(
        babylon_static
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        "${ONNXRUNTIME_INSTALL_DIR}/include/onnxruntime"
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/submodules/json/single_include
    )
endif()

add_dependencies(babylon onnxruntime_build copy_onnxruntime)
if(WIN32)
    add_dependencies(babylon_static onnxruntime_build)
endif()

if(WIN32)
    target_link_libraries(
        babylon
        onnxruntime
    )
    target_link_libraries(
        babylon_static
        onnxruntime
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
    add_subdirectory(src/cli)
endif()

install(
    TARGETS babylon
    RUNTIME DESTINATION ${BABYLON_INSTALL_LIBRARY_DIR}
    LIBRARY DESTINATION ${BABYLON_INSTALL_LIBRARY_DIR}
    ARCHIVE DESTINATION ${BABYLON_INSTALL_LIBRARY_DIR}
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION ${BABYLON_INSTALL_INCLUDE_DIR}
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/"
        DESTINATION "${BABYLON_INSTALL_RUNTIME_DIR}/data"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models")
    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/models/"
        DESTINATION "${BABYLON_INSTALL_RUNTIME_DIR}/models"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/voices")
    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/voices/"
        DESTINATION "${BABYLON_INSTALL_RUNTIME_DIR}/voices"
    )
endif()

if(WIN32)
    install(
        FILES "${ONNXRUNTIME_RUNTIME}"
        DESTINATION ${BABYLON_INSTALL_LIBRARY_DIR}
    )
elseif(APPLE)
    install(
        DIRECTORY "${ONNXRUNTIME_INSTALL_DIR}/lib/"
        DESTINATION ${BABYLON_INSTALL_LIBRARY_DIR}
        FILES_MATCHING
        PATTERN "libonnxruntime*.dylib"
    )
else()
    install(
        FILES "${ONNXRUNTIME_LIB}"
        DESTINATION ${BABYLON_INSTALL_LIBRARY_DIR}
    )
endif()
