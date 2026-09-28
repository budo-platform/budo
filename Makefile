# Makefile wrapper for CMake build
# Works on Linux, macOS, and Windows (with make/mingw32-make)

.DEFAULT_GOAL := all

BUILD_DIR := build
BUILD_TYPE ?= Release
CMAKE_FLAGS ?=
MAKE_COMMAND ?= $(MAKE)
DEPENDENCY_LOCK := cmake/BudoDependencyLock.cmake
LOCKED_QUICKJS_IMPL := $(shell sed -nE 's/^set\(BUDO_QUICKJS_IMPL_DEFAULT "([^"]+)"\)$$/\1/p' $(DEPENDENCY_LOCK))
QUICKJS_IMPL ?= $(LOCKED_QUICKJS_IMPL)
PROJECT_ROOT := $(shell pwd -L)
BUDO_ANDROID_PACK_ROOT ?= private/android
ANDROID_PACK_RESOLVED_MAKE := $(BUILD_DIR)/generated/android-feature-pack.mk
ANDROID_PACK_RESOLVE_STATUS := $(shell mkdir -p "$(dir $(ANDROID_PACK_RESOLVED_MAKE))"; \
	python3 scripts/resolve-feature-pack.py --repo "$(CURDIR)" \
		--pack-root "$(BUDO_ANDROID_PACK_ROOT)" --format make \
		--output "$(ANDROID_PACK_RESOLVED_MAKE)" >/dev/null; echo $$?)
ifneq ($(ANDROID_PACK_RESOLVE_STATUS),0)
$(error Android feature-pack resolution failed; run scripts/resolve-feature-pack.py for details)
endif
include $(ANDROID_PACK_RESOLVED_MAKE)
BUDO_ANDROID_PACK_ROOT := $(BUDO_ANDROID_PACK_ROOT_RESOLVED)
ifneq ($(strip $(BUDO_ANDROID_PACK_MAKE_INCLUDE)),)
include $(BUDO_ANDROID_PACK_MAKE_INCLUDE)
endif

# Detect OS
ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
    CMAKE_GENERATOR ?= "MinGW Makefiles"
    EXECUTABLE := $(BUILD_DIR)/budo.exe
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        DETECTED_OS := macOS
    else
        DETECTED_OS := Linux
    endif
    CMAKE_GENERATOR ?= "Unix Makefiles"
    EXECUTABLE := $(BUILD_DIR)/budo
endif

.PHONY: all configure build clean rebuild run debug release install help stats generate-contract-artifacts generate-types graphics-smoke native-sdk native-ci macos-cli macos-app macos-dmg linux-cli linux-appimage linux-flatpak linux-flatpak-install android-support-bundle android-screenshots android-capture-screenshots windows-setup windows-build windows-rebuild windows-clean web-setup-emscripten web-build web-clean web-run web-export web-serve web-build-runtime web-runtime-build playground playground-serve website website-demos website-demo-apks docs everything release-hashes

# Default target
all: build

# Configure CMake
configure:
	@echo "Configuring for $(DETECTED_OS)..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -G $(CMAKE_GENERATOR) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DBUDO_QUICKJS_IMPL=$(QUICKJS_IMPL) \
		$(BUDO_ANDROID_CMAKE_FLAGS) \
		$(CMAKE_FLAGS) \
		..

# Build project
build: configure
	@echo "Building..."
	@cmake --build $(BUILD_DIR) --config $(BUILD_TYPE) -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Clean build directory
clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR)

# Clean and rebuild
rebuild: clean build

# Run the executable
run: build
	@echo "Running budo..."
	@$(EXECUTABLE) $(ARGS)

# Run with example
example: build
	@echo "Running example..."
	@$(EXECUTABLE) examples/demo

# Run all demos one after the other
EXAMPLES := $(sort $(wildcard examples/*/))
run-all: build
	@for dir in $(EXAMPLES); do \
		echo "=== Running $$dir ==="; \
		$(EXECUTABLE) $$dir || true; \
	done

# Debug build
debug:
	@$(MAKE) BUILD_TYPE=Debug build

# Release build
release:
	@$(MAKE) BUILD_TYPE=Release build

# Under sudo, keep the build tree owned by the invoking user and reserve root
# privileges for copying files into the system install prefix.
install:
	@MAKE_COMMAND="$(MAKE_COMMAND)" \
	 BUILD_TYPE="$(BUILD_TYPE)" CMAKE_FLAGS="$(CMAKE_FLAGS)" \
	 QUICKJS_IMPL="$(QUICKJS_IMPL)" BUDO_ANDROID_PACK_ROOT="$(BUDO_ANDROID_PACK_ROOT)" \
	 ./scripts/install.sh "$(BUILD_DIR)"

# Produce the current host's relocatable, versioned native SDK bundle. Release
# jobs should set BUDO_NATIVE_SDK_BUILD_ID and SOURCE_DATE_EPOCH through
# CMAKE_FLAGS/the environment.
native-sdk:
	@$(MAKE) BUILD_TYPE=RelWithDebInfo build
	@rm -rf $(BUILD_DIR)/native-sdk/stage
	@cmake --install $(BUILD_DIR) --prefix $(BUILD_DIR)/native-sdk/stage --component NativeSDK --config RelWithDebInfo
	@tuple=$$(grep '^BUDO_NATIVE_TARGET_TUPLE:' $(BUILD_DIR)/CMakeCache.txt | cut -d= -f2); \
	 if [ -z "$$tuple" ]; then \
	   case "$(DETECTED_OS)-$$(uname -m 2>/dev/null || echo x86_64)" in \
	     macOS-arm64) tuple=macos-arm64-apple ;; macOS-*) tuple=macos-x86_64-apple ;; \
	     Linux-*) tuple=linux-x86_64-gnu ;; *) tuple=windows-x86_64-msvc ;; esac; \
	 fi; \
	 ext=tar.gz; [ "$(DETECTED_OS)" = Windows ] && ext=zip; \
	 baseline=$$(grep '^BUDO_NATIVE_PLATFORM_BASELINE:' $(BUILD_DIR)/CMakeCache.txt | cut -d= -f2); \
	 compiler=$$(grep '^BUDO_NATIVE_COMPILER_FAMILY:' $(BUILD_DIR)/CMakeCache.txt | cut -d= -f2); \
	 build_id=$$(grep '^BUDO_NATIVE_SDK_BUILD_ID:' $(BUILD_DIR)/CMakeCache.txt | cut -d= -f2); \
	 python3 scripts/package-native-sdk.py \
	   --stage $(BUILD_DIR)/native-sdk/stage \
	   --output $(BUILD_DIR)/native-sdk/budo-native-sdk-0.4.3-$$tuple.$$ext \
	   --version 0.4.3 --tuple $$tuple --compiler "$$compiler" --build-id "$$build_id" \
	   --baseline "$$baseline"

# Local equivalent of the supported-target native CI smoke workflow.
native-ci:
	@$(MAKE) BUILD_TYPE=RelWithDebInfo build
	@ctest --test-dir $(BUILD_DIR) -C RelWithDebInfo --output-on-failure
	@$(MAKE) native-sdk
	@rm -rf $(BUILD_DIR)/native-ci-app
	@$(EXECUTABLE) compile examples/12_native_c --offline \
		--sdk $(BUILD_DIR)/native-sdk/stage \
		--build-dir $(BUILD_DIR)/native-ci-app

# Generate checked-in type/support artifacts from API contracts
generate-contract-artifacts:
	@python3 scripts/generate-contract-artifacts.py --repo "$(CURDIR)"

# Generate TypeScript declarations from API contracts and JS binding sources
generate-types: generate-contract-artifacts
	@./scripts/generate-types.sh

graphics-smoke:
	@bash ./scripts/smoke-graphics-pipeline.sh

# Help
help:
	@echo "Available targets:"
	@echo "  all       - Configure and build (default)"
	@echo "  configure - Run CMake configuration"
	@echo "  build     - Build the project"
	@echo "  clean     - Remove build directory"
	@echo "  rebuild   - Clean and rebuild"
	@echo "  run       - Build and run (use ARGS=<project_dir> to specify project)"
	@echo "  example   - Build and run examples/demo"
	@echo "  run-all   - Build and run all examples sequentially"
	@echo "  native-ci - Test native support, package the SDK, and compile the starter"
	@echo "  debug     - Build with debug configuration"
	@echo "  release   - Build with release configuration"
	@echo "  install   - Install the executable"
	@echo "  stats     - Show colored project line/documentation statistics"
	@echo "  generate-contract-artifacts - Regenerate contract-derived types/support matrix"
	@echo "  generate-types - Regenerate types/budo.d.ts from contracts and C sources"
	@echo "  graphics-smoke - Build and smoke-check immediate graphics backends"
	@echo ""
	@echo "macOS targets:"
	@echo "  macos-cli - Stage standalone macOS CLI binary for website/install.sh"
	@echo "  macos-app - Create macOS .app bundle (build/Budo.app)"
	@echo "  macos-dmg - Create macOS .app bundle and .dmg disk image"
	@echo ""
	@echo "Linux targets:"
	@echo "  linux-cli              - Stage standalone Linux CLI binary for website/install.sh"
	@echo "  linux-appimage         - Create Linux AppImage (build/Budo-<arch>.AppImage)"
	@echo "  linux-flatpak          - Create Linux Flatpak bundle (build/Budo.flatpak)"
	@echo "  linux-flatpak-install  - Build and install the Flatpak into the user installation"
	@echo ""
	@echo "Android targets (Budo Pro):"
	@echo "  android-setup    - Download and build Skia for Android"
	@echo "  android-install  - Install APK on connected device"
	@echo "  android-clean    - Clean Android build outputs"
	@echo "  android-support-bundle - Stage website/download/budo-android-support-<sha256>.tar.gz"
	@echo "  android-screenshots    - Generate Play Store screenshot variants from SCREENSHOT_SRC or ANDROID_APP screenshot folders"
	@echo "  android-capture-screenshots - Capture raw Android screenshots via adb into an app screenshot folder"
	@echo "  Install private Android pack at $(BUDO_ANDROID_PACK_ROOT) to enable these targets."
	@echo ""
	@echo "Windows targets (Vagrant + QEMU VM):"
	@echo "  windows-box    - Build the Vagrant box from a Windows ISO (one-time)"
	@echo "  windows-setup  - Create and provision the Windows VM"
	@echo "  windows-build  - Build budo.exe inside the VM and fetch it (build/windows/)"
	@echo "  windows-rebuild - Re-sync source, rebuild, and fetch the binary"
	@echo "  windows-clean  - Destroy the Windows VM"
	@echo ""
	@echo "ONNX Runtime targets:"
	@echo "  onnx-setup         - Download ORT prebuilt C API for desktop (macOS / Linux)"
	@echo "  onnx-setup-android - Download ORT Android AAR and extract per-ABI libs (Budo Pro)"
	@echo ""
	@echo "Variables:"
	@echo "  BUILD_TYPE  - Debug or Release (default: Release)"
	@echo "  CMAKE_FLAGS - Additional CMake flags"
	@echo "  QUICKJS_IMPL - QuickJS implementation: bellard or ng (default: ng)"
	@echo "  ARGS        - Arguments to pass to the executable"
	@echo "  ANDROID_APP - App folder for Android targets (default: examples/shader_debug)"
	@echo "  WEB_APP     - App folder for web targets"
	@echo "  WIN_ISO     - Path to Windows ISO for windows-box target"
	@echo "  SCREENSHOT_SRC   - Screenshot file(s) or directory for android-screenshots"
	@echo "  SCREENSHOT_FLAGS - Extra flags for generate-screenshots.sh"
	@echo "  CAPTURE_FLAGS    - Extra flags for capture-screenshots.sh"
	@echo ""
	@echo "Examples:"
	@echo "  make"
	@echo "  make debug"
	@echo "  make run ARGS=examples/minimal"
	@echo "  make android-screenshots ANDROID_APP=examples/chess_clock # Budo Pro"
	@echo "  make android-screenshots SCREENSHOT_SRC=./screenshots # Budo Pro"
	@echo "  make android-capture-screenshots ANDROID_APP=examples/chess_clock CAPTURE_FLAGS='--count 5 --prompt' # Budo Pro"
	@echo "  make macos-dmg"
	@echo "  make windows-box WIN_ISO=~/Downloads/Win11.iso"
	@echo "  make windows-build"
	@echo "  make web-build"
	@echo "  make web-build WEB_APP=examples/hello_world"
	@echo "  make rebuild QUICKJS_IMPL=ng"
	@echo "  make web-run WEB_APP=examples/hello_world"
	@echo "  make web-export WEB_APP=examples/demo"
	@echo "  make web-serve WEB_APP=examples/demo"
	@echo ""
	@echo "Web targets (Emscripten):"
	@echo "  web-setup-emscripten - Download project-local Emscripten SDK (third_party/emsdk)"
	@echo "  web-setup-skia     - Build Skia for WebAssembly (required once before web-build)"
	@echo "  web-build          - Build WASM web target (set WEB_APP=<folder> to preload assets)"
	@echo "  web-run            - Build and serve locally on port 8080"
	@echo "  web-export         - Package a deployable web folder (set WEB_APP=<folder>)"
	@echo "  web-serve          - Export and serve locally on port 8080"
	@echo "  web-build-runtime  - Build base web runtime for --export-web embedding and website/"
	@echo "  web-runtime-build  - Alias for web-build-runtime"
	@echo "  web-clean          - Remove web build directory"
	@echo ""
	@echo "Playground (interactive web demo):"
	@echo "  playground         - Build web runtime and stage playground in website/"
	@echo "  playground-serve   - Serve website/ on port 8080 (includes playground)"
	@echo ""
	@echo "Website assets:"
	@echo "  website            - Regenerate website icons from budo-icon.svg"
	@echo "  website-demos      - Sync examples, refresh index demos, and generate demo pages"
	@echo "  website-demo-apks  - Build Android APK downloads for website demos (Budo Pro)"
	@echo "  docs               - Build documentation site (markdown -> website/doc/)"
	@echo ""
	@echo "Full release pipeline:"
	@echo "  everything         - Build types, web runtime, native binary,"
	@echo "                       website (icons+docs+playground), platform release bundles,"
	@echo "                       and SHA-256 hashes (dist/SHA256SUMS.txt)"
	@echo "  release-hashes     - Recompute dist/SHA256SUMS.txt for existing artifacts"

# Colorful repository inventory and line counts for tracked project text files.
stats:
	@set -e; \
	if [ -n "$$NO_COLOR" ]; then \
	  reset=''; bold=''; dim=''; red=''; green=''; yellow=''; blue=''; magenta=''; cyan=''; \
	else \
	  reset=$$(printf '\033[0m'); bold=$$(printf '\033[1m'); dim=$$(printf '\033[2m'); \
	  red=$$(printf '\033[31m'); green=$$(printf '\033[32m'); yellow=$$(printf '\033[33m'); \
	  blue=$$(printf '\033[34m'); magenta=$$(printf '\033[35m'); cyan=$$(printf '\033[36m'); \
	fi; \
	files_matching() { git ls-files | awk -v re="$$1" '$$0 ~ re'; }; \
	file_total() { files_matching "$$1" | awk 'END { print NR + 0 }'; }; \
	line_total() { files_matching "$$1" | while IFS= read -r file; do [ -f "$$file" ] && wc -l < "$$file"; done | awk '{ total += $$1 } END { print total + 0 }'; }; \
	byte_total() { files_matching "$$1" | while IFS= read -r file; do [ -f "$$file" ] && wc -c < "$$file"; done | awk '{ total += $$1 } END { if (total >= 1048576) printf "%.1f MB", total / 1048576; else if (total >= 1024) printf "%.1f KB", total / 1024; else printf "%d B", total + 0 }'; }; \
	dir_total() { find "$$1" -mindepth 1 -maxdepth 1 -type d $$2 2>/dev/null | awk 'END { print NR + 0 }'; }; \
	row() { printf '  %b%-27s%b %b%8s%b lines  %b%5s%b files\n' "$$bold" "$$1" "$$reset" "$$green" "$$2" "$$reset" "$$cyan" "$$3" "$$reset"; }; \
	text_re='.*\.(c|h|cc|cpp|hpp|m|mm|js|ts|lua|sh|py|cmake|gradle|properties|xml|html|css|md|json|yml|yaml)$$|^(CMakeLists\.txt|Makefile)$$'; \
	native_re='^src/.*\.(c|h|cc|cpp|hpp|m|mm|md)$$'; \
	web_re='^web/.*\.(c|h|js|ts|html|css|md)$$'; \
	examples_re='^examples/.*\.(js|ts|lua|c|h|html|css|json|md)$$'; \
	android_re='^android/.*\.(java|kt|xml|gradle|properties|c|h|cpp)$$|^documentation/android-packaging\.md$$'; \
	docs_re='.*\.(md|markdown)$$|^documentation/.*\.(html|css|py)$$'; \
	website_re='^website/.*\.(html|css|js|ts|md|json|svg)$$|^documentation/.*\.(html|css|js|md|py)$$'; \
	scripts_re='^(scripts|cmake|packaging|packer)/.*\.(sh|py|cmake|json|pkr\.hcl)$$|^(CMakeLists\.txt|Makefile)$$'; \
	types_re='^(types/.*\.d\.ts|budo\.d\.ts)$$'; \
	asset_re='.*\.(png|jpg|jpeg|gif|webp|svg|ico|wav|mp3|ogg|mid|ttf|obj|mtl|glb|gltf|wasm)$$'; \
	total_lines=$$(line_total "$$text_re"); total_files=$$(file_total "$$text_re"); \
	native_lines=$$(line_total "$$native_re"); native_files=$$(file_total "$$native_re"); \
	web_lines=$$(line_total "$$web_re"); web_files=$$(file_total "$$web_re"); \
	examples_lines=$$(line_total "$$examples_re"); examples_files=$$(file_total "$$examples_re"); \
	android_lines=$$(line_total "$$android_re"); android_files=$$(file_total "$$android_re"); \
	docs_lines=$$(line_total "$$docs_re"); docs_files=$$(file_total "$$docs_re"); \
	website_lines=$$(line_total "$$website_re"); website_files=$$(file_total "$$website_re"); \
	scripts_lines=$$(line_total "$$scripts_re"); scripts_files=$$(file_total "$$scripts_re"); \
	types_lines=$$(line_total "$$types_re"); types_files=$$(file_total "$$types_re"); \
	example_dirs=$$(find examples -mindepth 1 -maxdepth 1 -type d ! -name '_*' 2>/dev/null | awk 'END { print NR + 0 }'); \
	src_modules=$$(dir_total src ''); \
	doc_pages=$$(file_total '^documentation/pages/.*\.(md|markdown)$$'); \
	script_count=$$(file_total '^scripts/.*\.(sh|py)$$'); \
	asset_files=$$(file_total "$$asset_re"); asset_size=$$(byte_total "$$asset_re"); \
	printf '%b%s%b\n' "$$bold$$magenta" '📊 Budo project stats' "$$reset"; \
	printf '%b%s%b\n\n' "$$dim" 'Tracked files only; generated build outputs are ignored.' "$$reset"; \
	row '🧠 Native runtime' "$$native_lines" "$$native_files"; \
	row '🌐 Web runtime' "$$web_lines" "$$web_files"; \
	row '🎮 Examples' "$$examples_lines" "$$examples_files"; \
	row '🤖 Android' "$$android_lines" "$$android_files"; \
	row '📚 Documentation' "$$docs_lines" "$$docs_files"; \
	row '🏠 Website' "$$website_lines" "$$website_files"; \
	row '🛠️  Scripts/tooling' "$$scripts_lines" "$$scripts_files"; \
	row '🧾 Type declarations' "$$types_lines" "$$types_files"; \
	printf '  %b%-27s%b %b%8s%b lines  %b%5s%b files\n\n' "$$bold" '✨ Total text' "$$reset" "$$yellow" "$$total_lines" "$$reset" "$$yellow" "$$total_files" "$$reset"; \
	printf '%b%s%b\n' "$$bold$$blue" '📦 Inventory' "$$reset"; \
	printf '  🎮 Example apps:      %b%s%b\n' "$$green" "$$example_dirs" "$$reset"; \
	printf '  🧩 Source modules:    %b%s%b\n' "$$green" "$$src_modules" "$$reset"; \
	printf '  📖 Doc pages:         %b%s%b\n' "$$green" "$$doc_pages" "$$reset"; \
	printf '  🧰 Helper scripts:    %b%s%b\n' "$$green" "$$script_count" "$$reset"; \
	printf '  🖼️  Asset files:       %b%s%b (%s)\n' "$$green" "$$asset_files" "$$reset" "$$asset_size"

# ── Web Build (Emscripten) ─────────────────────────────────────────────────────

WEB_BUILD_DIR := build-web
WEB_APP ?=
EMSDK_VERSION ?= 5.0.5
export BUDO_EMSDK_VERSION := $(EMSDK_VERSION)

# Download and prepare the project-local Emscripten SDK.
web-setup-emscripten:
	@bash ./scripts/setup-emscripten.sh

# Setup Skia for WASM (required before web-build)
web-setup-skia:
	@echo "Setting up Skia for WebAssembly..."
	@bash ./scripts/setup-skia-wasm.sh

# Build the Emscripten web target
web-build:
	@echo "Building web target..."
	@mkdir -p $(WEB_BUILD_DIR)
	@bash -c 'set -e; source ./scripts/emscripten-env.sh; \
	reset_web_build_if_emscripten_changed $(WEB_BUILD_DIR); \
	mkdir -p $(WEB_BUILD_DIR); \
	if [ -n "$(WEB_APP)" ]; then \
	  emcmake cmake -B $(WEB_BUILD_DIR) -S web \
	    -DBUDO_ROOT=$(PROJECT_ROOT) \
	    -DCMAKE_BUILD_TYPE=Release \
	    -DBUDO_QUICKJS_IMPL=$(QUICKJS_IMPL) \
	    -DPROJECT_ASSET_DIR=$(PROJECT_ROOT)/$(WEB_APP); \
	else \
	  emcmake cmake -B $(WEB_BUILD_DIR) -S web \
	    -DBUDO_ROOT=$(PROJECT_ROOT) \
	    -DBUDO_QUICKJS_IMPL=$(QUICKJS_IMPL) \
	    -DCMAKE_BUILD_TYPE=Release; \
	fi; \
	emmake cmake --build $(WEB_BUILD_DIR) -j$$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)'

# Build and serve locally
web-run: web-build
	@echo "Serving web build at http://localhost:8080/budo.html"
	cd $(WEB_BUILD_DIR) && python3 -m http.server 8080

# Package a self-contained deployable web folder
web-export:
	@test -n "$(WEB_APP)" || (echo "Usage: make web-export WEB_APP=examples/demo" && exit 1)
	./scripts/build-web.sh $(WEB_APP)

# Export and serve locally
web-serve: web-export
	@echo "Serving web export at http://localhost:8080"
	cd dist/web && python3 -m http.server 8080

# Clean web build
web-clean:
	@echo "Cleaning web build..."
	@rm -rf $(WEB_BUILD_DIR) dist/web

# Build the base web runtime (without project assets) for embedding into the
# native binary and for the website playground/demos. Uses the project-local
# Emscripten SDK. After this, run 'make rebuild' to embed the runtime into the
# native budo binary.
web-build-runtime:
	@echo "Building web runtime (no project assets)..."
	@mkdir -p $(WEB_BUILD_DIR)
	@bash -c 'set -e; source ./scripts/emscripten-env.sh; \
	reset_web_build_if_emscripten_changed $(WEB_BUILD_DIR); \
	mkdir -p $(WEB_BUILD_DIR); \
	emcmake cmake -B $(WEB_BUILD_DIR) -S web \
	    -DBUDO_ROOT=$(PROJECT_ROOT) \
	    -DCMAKE_BUILD_TYPE=Release \
	    -DBUDO_QUICKJS_IMPL=$(QUICKJS_IMPL) \
	    -DPROJECT_ASSET_DIR=""; \
	emmake cmake --build $(WEB_BUILD_DIR) -j$$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)'
	@mkdir -p web/runtime website
	cp $(WEB_BUILD_DIR)/budo.js  web/runtime/budo.js
	cp $(WEB_BUILD_DIR)/budo.wasm web/runtime/budo.wasm
	cp $(WEB_BUILD_DIR)/budo.js  website/budo.js
	cp $(WEB_BUILD_DIR)/budo.wasm website/budo.wasm
	@echo ""
	@echo "Web runtime built and copied to web/runtime/ and website/"
	@echo "Now run 'make rebuild' to embed it into the native binary."

web-runtime-build: web-build-runtime

# ── Playground (interactive web demo with editor) ─────────────────────────────

# Build the playground: compile the base web runtime and stage it in website/
playground: web-build-runtime
	@echo "Staging playground runtime..."
	cp $(WEB_BUILD_DIR)/budo.js  website/budo.js
	cp $(WEB_BUILD_DIR)/budo.wasm website/budo.wasm
	@echo ""
	@echo "Playground ready!  Serve with:  make playground-serve"
	@echo "Then open http://localhost:8080/playground.html"

# Serve the website directory (includes the playground)
playground-serve:
	@echo "Serving website at http://localhost:8080"
	@echo "Open http://localhost:8080/playground.html"
	cd website && python3 -m http.server 8080

# ── Website assets ───────────────────────────────────────────────────────────

# Regenerate icon assets in website/ from the canonical budo-icon.svg.
# Requires librsvg (`brew install librsvg`) or ImageMagick.
website:
	@./scripts/build-website-icons.sh

# Sync examples into website/demos/ and regenerate the homepage demos carousel.
website-demos:
	@./scripts/sync-website-demos.sh
	@./scripts/update-website-demos.py

# Build APK downloads for generated website demo pages. Use flags like:
#   make website-demo-apks WEBSITE_DEMO_APK_FLAGS="--demo chess_clock"
website-demo-apks: build
	@if [ ! -f "$(BUDO_ANDROID_PACK_ROOT)/scripts/build-website-demo-apks.sh" ]; then \
		echo "Android APK demo downloads are a Budo Pro feature."; \
		echo "Install the private Android feature pack under $(BUDO_ANDROID_PACK_ROOT) to enable this target."; \
		exit 1; \
	fi
	@"$(BUDO_ANDROID_PACK_ROOT)/scripts/build-website-demo-apks.sh" $(WEBSITE_DEMO_APK_FLAGS)

# Build the documentation site (markdown -> HTML in website/doc/).
docs: generate-contract-artifacts
	@echo "Building documentation site..."
	@cd documentation && ./build.py

# ── Everything: full release pipeline ────────────────────────────────────────
#
# Builds, in order:
#   1. TypeScript declarations  (types/budo.d.ts)
#   2. Web runtime              (web/runtime/budo.{js,wasm})
#   3. Android support bundle and pinned digest (when Budo Pro is installed)
#   4. Native budo binary   (with budo-llm.md, budo.d.ts, web runtime,
#                                and the Pro Android template when installed)
#   5. Website                  (icons, documentation, playground)
#   6. Platform release bundles (macOS .app+.dmg on Darwin,
#                                AppImage + Flatpak on Linux,
#                                Windows .exe via Vagrant VM if available)
#   7. SHA-256 hashes for every produced artifact (dist/SHA256SUMS.txt)
#
# Targets that require platform-specific tooling (Vagrant for Windows, flatpak
# on Linux, codesign on macOS, …) are invoked with `-` so a missing toolchain
# does not abort the rest of the pipeline.

# Platform detection for which release bundles to attempt.
ifeq ($(DETECTED_OS),macOS)
EVERYTHING_PLATFORM_TARGETS := macos-cli macos-app macos-dmg
else ifeq ($(DETECTED_OS),Linux)
EVERYTHING_PLATFORM_TARGETS := linux-cli linux-appimage linux-flatpak
else
EVERYTHING_PLATFORM_TARGETS :=
endif

everything:
	@echo "════════════════════════════════════════════════════════════════"
	@echo " Budo — full release build"
	@echo "════════════════════════════════════════════════════════════════"
	@echo ""
	@echo "[1/7] Regenerating TypeScript declarations..."
	@$(MAKE) generate-types
	@echo ""
	@echo "[2/7] Building web runtime (Emscripten)..."
	@$(MAKE) web-build-runtime
	@echo ""
	@echo "[3/7] Building and pinning Android support bundle..."
ifeq ($(strip $(BUDO_ANDROID_EVERYTHING_TARGETS)),)
	@echo "  (private Android pack not available)"
else
	@$(MAKE) $(BUDO_ANDROID_EVERYTHING_TARGETS)
endif
	@echo ""
	@echo "[4/7] Rebuilding native budo binary with embedded assets..."
	@$(MAKE) rebuild
	@echo ""
	@echo "[5/7] Building website (icons, docs, playground)..."
	@-$(MAKE) website
	@$(MAKE) website-demos
	@$(MAKE) docs
	@$(MAKE) playground
	@echo ""
	@echo "[6/7] Building platform release bundles for $(DETECTED_OS)..."
ifeq ($(strip $(EVERYTHING_PLATFORM_TARGETS)),)
	@echo "  (no platform release bundles defined for $(DETECTED_OS))"
else
	@for t in $(EVERYTHING_PLATFORM_TARGETS); do \
	    echo ""; echo "  → $$t"; \
	    $(MAKE) $$t || echo "  ! $$t failed (continuing)"; \
	done
endif
	@echo ""
	@echo "  → windows-build (requires Vagrant + windows-11-qemu box)"
	@-$(MAKE) windows-build || echo "  ! windows-build skipped/failed (continuing)"
	@echo ""
	@echo "[7/7] Computing release hashes..."
	@$(MAKE) release-hashes
	@echo ""
	@echo "════════════════════════════════════════════════════════════════"
	@echo " Done. Artifacts under build/, dist/, website/."
	@echo "════════════════════════════════════════════════════════════════"

# Compute SHA-256 hashes for every release artifact present on disk.
# Writes dist/SHA256SUMS.txt with one line per artifact.
release-hashes:
	@mkdir -p dist
	@rm -f dist/SHA256SUMS.txt
	@if command -v sha256sum >/dev/null 2>&1; then HASH="sha256sum"; \
	 elif command -v shasum     >/dev/null 2>&1; then HASH="shasum -a 256"; \
	 else echo "No sha256sum/shasum available" >&2; exit 1; fi; \
	for f in \
	    $(EXECUTABLE) \
	    build/Budo.dmg \
	    website/download/Budo.dmg \
	    build/Budo-*.AppImage \
	    build/Budo.flatpak \
	    website/download/Budo-linux.AppImage \
	    website/download/Budo.flatpak \
	    website/download/budo-macos-* \
	    website/download/budo-linux-* \
	    website/download/budo-android-support-*.tar.gz \
	    website/install.sh \
	    build/windows/budo.exe \
	    web/runtime/budo.js \
	    web/runtime/budo.wasm \
	    types/budo.d.ts ; do \
	    if [ -f "$$f" ]; then \
	        ( cd "$$(dirname "$$f")" && $$HASH "$$(basename "$$f")" ) >> dist/SHA256SUMS.txt; \
	    fi; \
	done
	@if [ -s dist/SHA256SUMS.txt ]; then \
	    echo "Wrote dist/SHA256SUMS.txt:"; \
	    sed 's/^/  /' dist/SHA256SUMS.txt; \
	else \
	    echo "No release artifacts found to hash."; \
	fi

# ── Windows Build (Vagrant + QEMU VM) ─────────────────────────────────────────

.PHONY: windows-setup windows-build windows-rebuild windows-clean windows-box

# Build the windows-11-qemu Vagrant box from an ISO (one-time, ~45 min)
# Usage: make windows-box WIN_ISO=/path/to/Win11.iso
WIN_ISO ?=
windows-box:
	@if [ -z "$(WIN_ISO)" ]; then echo "Usage: make windows-box WIN_ISO=/path/to/Win11.iso"; exit 1; fi
	@./packer/windows-11-qemu/build-box.sh "$(WIN_ISO)"

# Provision the Windows VM (one-time setup)
windows-setup:
	@./scripts/build-windows.sh --setup-only

# Full Windows build: start VM (if needed), sync, build, fetch binary
windows-build:
	@./scripts/build-windows.sh

# Re-sync and rebuild (VM must already be running)
windows-rebuild:
	@./scripts/build-windows.sh --build-only

# Destroy the Windows VM
windows-clean:
	@cd "$(CURDIR)" && vagrant destroy windows-build -f 2>/dev/null || true
	@rm -rf build/windows

# ── macOS App Bundle / DMG ────────────────────────────────────────────────────

.PHONY: macos-cli macos-app macos-dmg

# Stage the standalone macOS CLI binary consumed by website/install.sh.
macos-cli: build
	@if [ "$$(uname -s)" != "Darwin" ]; then \
	    echo "macos-cli must be run on macOS" >&2; \
	    exit 1; \
	fi; \
	arch="$$(uname -m)"; \
	case "$$arch" in \
	    x86_64|amd64) arch="x86_64" ;; \
	    aarch64|arm64) arch="aarch64" ;; \
	    *) echo "Unsupported macOS architecture: $$arch" >&2; exit 1 ;; \
	esac; \
	mkdir -p website/download; \
	cp "$(EXECUTABLE)" "website/download/budo-macos-$$arch"; \
	chmod 0755 "website/download/budo-macos-$$arch"; \
	echo "Website download staged: website/download/budo-macos-$$arch"

# Create a macOS .app bundle in build/Budo.app
macos-app: build
	@./scripts/bundle-macos.sh

# Create a macOS .app bundle and .dmg disk image in build/
macos-dmg: build
	@./scripts/bundle-macos.sh --dmg

# ── Linux CLI / AppImage / Flatpak ───────────────────────────────────────────

.PHONY: linux-cli linux-appimage

# Stage the standalone Linux CLI binary consumed by website/install.sh.
linux-cli: build
	@if [ "$$(uname -s)" != "Linux" ]; then \
	    echo "linux-cli must be run on Linux" >&2; \
	    exit 1; \
	fi; \
	arch="$$(uname -m)"; \
	case "$$arch" in \
	    x86_64|amd64) arch="x86_64" ;; \
	    aarch64|arm64) arch="aarch64" ;; \
	    *) echo "Unsupported Linux architecture: $$arch" >&2; exit 1 ;; \
	esac; \
	mkdir -p website/download; \
	cp "$(EXECUTABLE)" "website/download/budo-linux-$$arch"; \
	chmod 0755 "website/download/budo-linux-$$arch"; \
	echo "Website download staged: website/download/budo-linux-$$arch"

# Create a Linux AppImage in build/Budo-<arch>.AppImage
linux-appimage: build
	@./scripts/bundle-linux-appimage.sh

# Create a Linux Flatpak bundle in build/Budo.flatpak
linux-flatpak: build
	@./scripts/bundle-linux-flatpak.sh

# Build the Flatpak and install it into the user installation
linux-flatpak-install: build
	@./scripts/bundle-linux-flatpak.sh --install

# ── Android Build Targets (Budo Pro) ──────────────────────────────────────

ANDROID_DIR := $(BUDO_ANDROID_PACK_ROOT)/android
ANDROID_APP ?= examples/shader_debug
SCREENSHOT_SRC ?=

.PHONY: android-setup android-install android-clean android-debug android-support-bundle

ifeq ($(BUDO_ANDROID_PACK_AVAILABLE),0)

define budo_android_pro_unavailable
	@echo "Android packaging is a Budo Pro feature."
	@echo "Install the private Android feature pack under $(BUDO_ANDROID_PACK_ROOT) to enable this target."
	@exit 1
endef

# Setup Skia for Android
android-setup:
	$(budo_android_pro_unavailable)

# Install APK on device and run
android-install:
	$(budo_android_pro_unavailable)

# Clean Android build
android-clean:
	$(budo_android_pro_unavailable)

android-support-bundle:
	$(budo_android_pro_unavailable)

android-screenshots:
	$(budo_android_pro_unavailable)

android-capture-screenshots:
	$(budo_android_pro_unavailable)

endif

# ── ONNX Runtime Setup ────────────────────────────────────────────────────────

.PHONY: onnx-setup onnx-setup-android

# Download ORT prebuilt C API for the current desktop platform (macOS / Linux)
onnx-setup:
	@./scripts/setup-onnx.sh

# Download ORT Android AAR and extract per-ABI .so + C API headers
onnx-setup-android:
	@if [ ! -x "$(BUDO_ANDROID_PACK_ROOT)/scripts/setup-onnx-android.sh" ]; then \
		echo "ONNX Runtime setup for Android is a Budo Pro feature."; \
		echo "Install the private Android feature pack under $(BUDO_ANDROID_PACK_ROOT) to enable this target."; \
		exit 1; \
	fi
	@"$(BUDO_ANDROID_PACK_ROOT)/scripts/setup-onnx-android.sh"
