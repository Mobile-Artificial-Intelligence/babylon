# Build Configuration Changes

## Summary

The CMakeLists.txt has been updated to build ONNX Runtime from source instead of downloading prebuilt binaries.

## Changes Made

### Before (Prebuilt binaries)
- Downloaded prebuilt ONNX Runtime binaries from GitHub releases
- Extracted archives and copied libraries to lib directory
- Supported multiple platforms with platform-specific URLs

### After (Source build)
- Uses `ExternalProject_Add` to download ONNX Runtime source code
- Builds ONNX Runtime with optimized settings for CPU inference
- Installs the built library and headers to proper locations
- Copies built libraries to the babylon lib directory

## Build Configuration

The ONNX Runtime build is configured with the following options:
- Shared library build enabled (`onnxruntime_BUILD_SHARED_LIB=ON`)
- All language bindings disabled (Python, C#, Java, Node.js, etc.)
- All execution providers disabled except CPU
- Tests and benchmarks disabled for faster build
- Minimal feature set for inference only

## Build Process

1. During CMake configuration, ExternalProject_Add sets up the ONNX Runtime build
2. When building, it:
   - Downloads ONNX Runtime source code (v1.18.1)
   - Configures ONNX Runtime with specified options
   - Builds the shared library
   - Installs headers and library to designated directories
   - Copies the built library to babylon's lib directory

## Build Time

Building ONNX Runtime from source will significantly increase build time compared to downloading prebuilt binaries. The first build may take 30+ minutes depending on your system.

## Benefits of Source Build

1. **Customization**: Can enable/disable specific features as needed
2. **Optimization**: Can be built with specific compiler optimizations
3. **Consistency**: Ensures consistent toolchain and compilation flags
4. **Security**: No dependency on external prebuilt binaries
5. **Platform support**: Can build for platforms not covered by official releases

## Development Workflow

- Clean builds: Remove the entire build directory to rebuild ONNX Runtime
- Incremental builds: Only babylon will rebuild if ONNX Runtime is unchanged
- CMake cache: ONNX Runtime configuration is cached and won't rebuild unless changed