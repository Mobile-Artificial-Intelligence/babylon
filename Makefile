# Variables
BUILD_DIR = build
BIN_DIR = bin
INSTALL_DIR ?= $(CURDIR)/install
CMAKE_FLAGS = -DBABYLON_BIN_DIR=$(CURDIR)/$(BIN_DIR)
CORES ?= 1

# Default target
all: cli

# Library only
lib:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release \
		-DBUILD_CLI=OFF ..
	@$(MAKE) -C $(BUILD_DIR) -j$(CORES)

# CLI (implies lib)
cli:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release \
		-DBUILD_CLI=ON ..
	@$(MAKE) -C $(BUILD_DIR) -j$(CORES)

# Release build — alias for cli
release: cli

# Install into a prefix while preserving the bin/ layout
install: cli
	@cmake --install $(BUILD_DIR) --prefix $(INSTALL_DIR)

# Source build
source:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release \
		-DBUILD_CLI=ON -DBABYLON_BUILD_SOURCE=ON ..
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
		-DBUILD_CLI=ON ..
	@$(MAKE) -C $(BUILD_DIR) -j$(CORES)

# Clean build directory
clean:
	@$(RM) -r $(BUILD_DIR)
	@$(RM) -r $(BIN_DIR)
	@$(RM) -r $(CURDIR)/lib

.PHONY: all lib cli release install source android debug clean
