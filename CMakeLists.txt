cmake_minimum_required(VERSION 3.18)

project(
    babylon
    VERSION 0.0.2
    DESCRIPTION "Babylon Neural Text To Speech Engine"
    HOMEPAGE_URL "https://github.com/danemadsen/babylon.cpp"
    LANGUAGES CXX
)

set(BABYLON_LIB_INSTALL_DIR ${CMAKE_CURRENT_LIST_DIR}/lib CACHE PATH "Babylon library install directory")

# Enable C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set compiler options to support UTF-8 encoding
if(MSVC)
    add_compile_options("/utf-8")
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

# Set the output directory for the shared library
set_target_properties(
    babylon
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${BABYLON_LIB_INSTALL_DIR}
)

# if linux or windows
if(NOT APPLE AND NOT ANDROID)
    set_target_properties(
        babylon
        PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# Build ONNX Runtime from source using CMake
include(ExternalProject)

set(ONNXRUNTIME_VERSION "1.18.1")

# Set the ONNX Runtime build directory
set(ONNXRUNTIME_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/onnxruntime-build)
set(ONNXRUNTIME_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/onnxruntime-install)

# Use ExternalProject to download and build ONNX Runtime
ExternalProject_Add(
    onnxruntime_external
    GIT_REPOSITORY https://github.com/microsoft/onnxruntime.git
    GIT_TAG v${ONNXRUNTIME_VERSION}
    GIT_SHALLOW TRUE
    PREFIX ${ONNXRUNTIME_BUILD_DIR}
    SOURCE_SUBDIR cmake
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${ONNXRUNTIME_INSTALL_DIR}
        -Donnxruntime_BUILD_SHARED_LIB=ON
        -Donnxruntime_BUILD_UNIT_TESTS=OFF
        -Donnxruntime_RUN_ONNX_TESTS=OFF
        -Donnxruntime_GENERATE_TEST_REPORTS=OFF
        -Donnxruntime_ENABLE_PYTHON=OFF
        -Donnxruntime_BUILD_CSHARP=OFF
        -Donnxruntime_BUILD_JAVA=OFF
        -Donnxruntime_BUILD_NODEJS=OFF
        -Donnxruntime_BUILD_OBJC=OFF
        -Donnxruntime_BUILD_APPLE_FRAMEWORK=OFF
        -Donnxruntime_USE_DNNL=OFF
        -Donnxruntime_USE_NNAPI_BUILTIN=OFF
        -Donnxruntime_USE_RKNPU=OFF
        -Donnxruntime_USE_LLVM=OFF
        -Donnxruntime_ENABLE_MICROSOFT_INTERNAL=OFF
        -Donnxruntime_USE_VITISAI=OFF
        -Donnxruntime_USE_TENSORRT=OFF
        -Donnxruntime_USE_TENSORRT_BUILTIN_PARSER=OFF
        -Donnxruntime_TENSORRT_PLACEHOLDER_BUILDER=OFF
        -Donnxruntime_USE_TVM=OFF
        -Donnxruntime_TVM_CUDA_RUNTIME=OFF
        -Donnxruntime_TVM_USE_HASH=OFF
        -Donnxruntime_USE_MIGRAPHX=OFF
        -Donnxruntime_CROSS_COMPILING=OFF
        -Donnxruntime_DISABLE_CONTRIB_OPS=OFF
        -Donnxruntime_DISABLE_ML_OPS=OFF
        -Donnxruntime_DISABLE_RTTI=OFF
        -Donnxruntime_DISABLE_EXCEPTIONS=OFF
        -Donnxruntime_MINIMAL_BUILD=OFF
        -Donnxruntime_EXTENDED_MINIMAL_BUILD=OFF
        -Donnxruntime_MINIMAL_BUILD_CUSTOM_OPS=OFF
        -Donnxruntime_REDUCED_OPS_BUILD=OFF
        -Donnxruntime_ENABLE_LANGUAGE_INTEROP_OPS=OFF
        -Donnxruntime_USE_DML=OFF
        -Donnxruntime_USE_WINML=OFF
        -Donnxruntime_BUILD_MS_EXPERIMENTAL_OPS=OFF
        -Donnxruntime_USE_TELEMETRY=OFF
        -Donnxruntime_ENABLE_LTO=OFF
        -Donnxruntime_USE_ACL=OFF
        -Donnxruntime_USE_ARMNN=OFF
        -Donnxruntime_ENABLE_NVTX_PROFILE=OFF
        -Donnxruntime_ENABLE_TRAINING=OFF
        -Donnxruntime_ENABLE_TRAINING_OPS=OFF
        -Donnxruntime_ENABLE_TRAINING_TORCH_INTEROP=OFF
        -Donnxruntime_ENABLE_CPU_FP16_OPS=OFF
        -Donnxruntime_USE_NUPHAR=OFF
        -Donnxruntime_USE_EIGEN_THREADPOOL=OFF
        -Donnxruntime_BUILD_BENCHMARKS=OFF
        -Donnxruntime_USE_PREINSTALLED_EIGENVERSION=OFF
        -Donnxruntime_USE_EIGEN_FOR_BLAS=OFF
        -Donnxruntime_USE_OPENVINO=OFF
        -Donnxruntime_DEV_MODE=OFF
        -Donnxruntime_PYBIND_EXPORT_OPSCHEMA=OFF
        -Donnxruntime_ENABLE_MEMLEAK_CHECKER=OFF
        -Donnxruntime_ENABLE_ROCM_PROFILING=OFF
        -Donnxruntime_USE_ROCM=OFF
        -Donnxruntime_USE_COMPOSABLE_KERNEL=OFF
        -Donnxruntime_USE_TRITON_KERNEL=OFF
        -Donnxruntime_USE_WEBNN=OFF
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE}
    INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --target install
    BUILD_BYPRODUCTS 
        ${ONNXRUNTIME_INSTALL_DIR}/lib/libonnxruntime.so
        ${ONNXRUNTIME_INSTALL_DIR}/lib/libonnxruntime.dylib
        ${ONNXRUNTIME_INSTALL_DIR}/lib/onnxruntime.dll
)

# Create an imported target for the built ONNX Runtime library
add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION ${ONNXRUNTIME_INSTALL_DIR}/lib/libonnxruntime${CMAKE_SHARED_LIBRARY_SUFFIX}
)

# Set up dependencies
add_dependencies(babylon onnxruntime_external)
add_dependencies(onnxruntime onnxruntime_external)

# Copy the built library to our lib directory
add_custom_command(TARGET onnxruntime_external POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${ONNXRUNTIME_INSTALL_DIR}/lib/
        ${BABYLON_LIB_INSTALL_DIR}/
)

# Include ONNX Runtime headers
target_include_directories(
    babylon
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ONNXRUNTIME_INSTALL_DIR}/include
)

# Link with ONNX Runtime
if(APPLE)
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

# Include example directory if EXAMPLES flag is set
option(BUILD_EXAMPLES "Build examples" OFF)

if(BUILD_EXAMPLES)
    add_subdirectory(example)
endif()