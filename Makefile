# Variables
BUILD_DIR = build
BIN_DIR = bin
CMAKE_FLAGS = -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)
CORES ?= 1

# Default target — builds lib + examples + cli
all: lib example cli

# Library only
lib:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release \
		-DBUILD_EXAMPLES=OFF -DBUILD_CLI=OFF ..
	@$(MAKE) -C $(BUILD_DIR) -j$(CORES)

# Examples only (implies lib)
example:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release \
		-DBUILD_EXAMPLES=ON -DBUILD_CLI=OFF ..
	@$(MAKE) -C $(BUILD_DIR) -j$(CORES)

# CLI only (implies lib)
cli:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release \
		-DBUILD_EXAMPLES=OFF -DBUILD_CLI=ON ..
	@$(MAKE) -C $(BUILD_DIR) -j$(CORES)

# Release build (lib + examples + cli) — kept for compatibility
release: all

# Source build
source:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release \
		-DBUILD_EXAMPLES=ON -DBUILD_CLI=ON -DBABYLON_BUILD_SOURCE=ON ..
	@$(MAKE) -C $(BUILD_DIR) -j$(CORES)

# Android build
android:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=$(ANDROID_NDK)/build/cmake/android.toolchain.cmake \
		-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 ..
	@$(MAKE) -C $(BUILD_DIR) -j$(CORES)

# Debug build
debug:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_EXAMPLES=ON -DBUILD_CLI=ON ..
	@$(MAKE) -C $(BUILD_DIR) -j$(CORES)

# Clean build directory
clean:
	@$(RM) -r $(BUILD_DIR)
	@$(RM) -r $(BIN_DIR)
	@$(RM) -r $(CURDIR)/lib
	@$(RM) -r $(CURDIR)/download

.PHONY: all lib example cli release source android debug clean
