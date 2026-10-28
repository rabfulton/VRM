# VRM Desktop Overlay Viewer - X11 and native Wayland backends

CC = gcc
CXX = g++
CFLAGS ?= -g -O2
CXXFLAGS ?= -g -O2
override CFLAGS += -std=c11 -Wall -Wextra -MMD -MP
override CXXFLAGS += -std=c++20 -Wall -Wextra -MMD -MP
PYTHON ?= python3
PREFIX ?= /usr/local
DESTDIR ?=
LICENSE_NAME ?= domovushka
WINDOW_BACKEND ?= both
WAYLAND_PROBE_DIR = build/wayland-probe
ifeq ($(filter $(WINDOW_BACKEND),x11 wayland both),)
  $(error WINDOW_BACKEND must be x11, wayland or both)
endif
PLATFORM_PACKAGES = epoxy
PLATFORM_SRC = src/platform.c
ifneq ($(filter $(WINDOW_BACKEND),x11 both),)
  PLATFORM_PACKAGES += glfw3 x11 xext
  PLATFORM_SRC += src/platform_x11.c
  override CPPFLAGS += -DVRM_PLATFORM_X11
endif
ifneq ($(filter $(WINDOW_BACKEND),wayland both),)
  PLATFORM_PACKAGES += wayland-client wayland-egl egl cairo
  PLATFORM_SRC += src/platform_wayland.c src/gem_menu_cairo.c
  override CPPFLAGS += -DVRM_PLATFORM_WAYLAND
endif
override CPPFLAGS += $(shell pkg-config --cflags $(PLATFORM_PACKAGES))
INCLUDES = -Ideps/cglm/include -Ideps/tinygltf -Ideps/cjson -Ideps/stb

# Optional ozz animation sampling and leg IK
USE_OZZ ?= 1
DEPS_BUILD_DIR = deps/_build
OZZ_BUILD_DIR = $(DEPS_BUILD_DIR)/ozz
OZZ_LIBS = $(OZZ_BUILD_DIR)/src/animation/offline/libozz_animation_offline_rd.a \
           $(OZZ_BUILD_DIR)/src/animation/runtime/libozz_animation_rd.a \
           $(OZZ_BUILD_DIR)/src/base/libozz_base_rd.a

LDLIBS = $(shell pkg-config --libs $(PLATFORM_PACKAGES)) -lm -lstdc++ -lpthread -ldl

ifeq ($(USE_OZZ),1)
  INCLUDES += -Ideps/ozz-animation/include
  override CPPFLAGS += -DVRM_USE_OZZ=1
  LDLIBS += $(OZZ_LIBS)
endif

C_SRC = src/main.c src/app_paths.c $(PLATFORM_SRC) src/gem_menu.c src/renderer.c src/input_mask.c src/hit_capture.c src/audio.c src/event.c src/viseme.c src/camera.c src/gltf_loader.c src/bounds.c src/expressions.c src/pose.c src/ipc.c src/animator.c src/transition.c src/spring_bones.c src/anim_config.c src/settings.c deps/cjson/cJSON.c
CXX_SRC = src/gltf_loader_impl.cpp src/tinygltf_impl.cpp src/foot_lock.cpp
ifeq ($(USE_OZZ),1)
  CXX_SRC += src/ozz_bridge.cpp
endif

OBJ_DIR = build/obj/$(WINDOW_BACKEND)-ozz$(USE_OZZ)
C_OBJ = $(addprefix $(OBJ_DIR)/,$(C_SRC:.c=.o))
CXX_OBJ = $(addprefix $(OBJ_DIR)/,$(CXX_SRC:.cpp=.o))
OBJ = $(C_OBJ) $(CXX_OBJ)
ifneq ($(filter $(WINDOW_BACKEND),wayland both),)
  OBJ += $(WAYLAND_PROBE_DIR)/wlr-layer-shell-protocol.o $(WAYLAND_PROBE_DIR)/xdg-shell-protocol.o
endif

TARGET = vrm-overlay

.PHONY: FORCE all clean debug test test-sanitize test-ui test-speech test-chat test-fit test-desktop test-render test-assets test-springs test-zoom studio

all: $(TARGET)

.PHONY: install
install: all
	$(PYTHON) scripts/install.py --prefix "$(PREFIX)" --destdir "$(DESTDIR)" --license-name "$(LICENSE_NAME)"

FORCE:

$(TARGET): $(OBJ) FORCE
	$(CXX) $(LDFLAGS) $(OBJ) -o $@ $(LDLIBS)

ifeq ($(USE_OZZ),1)
$(OZZ_LIBS) &:
	cmake -S deps/ozz-animation -B $(OZZ_BUILD_DIR) -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="$(filter-out -MMD -MP,$(CXXFLAGS))" -Dozz_build_tools=OFF -Dozz_build_fbx=OFF -Dozz_build_gltf=OFF -Dozz_build_samples=OFF -Dozz_build_howtos=OFF -Dozz_build_tests=OFF
	cmake --build $(OZZ_BUILD_DIR) -j4

$(TARGET): $(OZZ_LIBS)
endif

$(OBJ_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

debug: CFLAGS += -O0 -DDEBUG
debug: CXXFLAGS += -O0 -DDEBUG
debug: clean all

-include $(OBJ:.o=.d)

# Headless transport/audio regressions; no display, model, voice, or API key needed.
TEST_BUILD_DIR ?= build/tests
TEST_SANITIZERS ?=
TEST_CFLAGS = -std=c11 -Wall -Wextra -g -O1 -Isrc -Ideps/cjson
ifneq ($(TEST_SANITIZERS),)
  TEST_CFLAGS += -fsanitize=$(TEST_SANITIZERS) -fno-omit-frame-pointer -fno-pie -no-pie
endif

$(TEST_BUILD_DIR):
	mkdir -p $@

$(TEST_BUILD_DIR)/ipc-server: tests/ipc_server.c src/ipc.c src/ipc.h deps/cjson/cJSON.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) tests/ipc_server.c src/ipc.c deps/cjson/cJSON.c -o $@ -lm

$(TEST_BUILD_DIR)/audio-test: tests/audio_test.c src/audio.c src/audio.h deps/miniaudio.h | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) tests/audio_test.c src/audio.c -o $@ -lm -lpthread -ldl

$(TEST_BUILD_DIR)/transition-test: tests/transition_test.c src/transition.c src/transition.h | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) tests/transition_test.c src/transition.c -o $@ -lm

$(TEST_BUILD_DIR)/spring-test: tests/spring_test.c src/spring_bones.c src/spring_bones.h src/gltf_loader.h | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) tests/spring_test.c src/spring_bones.c -o $@ -lm

$(TEST_BUILD_DIR)/viseme-default-test: tests/viseme_default_test.c src/viseme.c src/viseme.h | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) tests/viseme_default_test.c src/viseme.c -o $@ -lm

$(TEST_BUILD_DIR)/expression-test: tests/expression_test.c src/expressions.c src/gltf_loader.h src/viseme.c src/viseme.h | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) tests/expression_test.c src/expressions.c src/viseme.c -o $@ -lm

$(TEST_BUILD_DIR)/anim-config-test: tests/anim_config_test.c src/anim_config.c src/anim_config.h | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) tests/anim_config_test.c src/anim_config.c -o $@ -lm

$(TEST_BUILD_DIR)/bounds-test: tests/bounds_test.c src/bounds.c src/camera.c src/camera.h src/pose.c src/gltf_loader.h | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) -Ideps/cglm/include tests/bounds_test.c src/bounds.c src/camera.c src/pose.c -o $@ -lm

$(TEST_BUILD_DIR)/input-mask-test: tests/input_mask_test.c src/input_mask.c src/input_mask.h | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) tests/input_mask_test.c src/input_mask.c -o $@

$(TEST_BUILD_DIR)/settings-test: tests/settings_test.c src/settings.c src/settings.h | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) tests/settings_test.c src/settings.c -o $@

$(TEST_BUILD_DIR)/frame-clock-test: tests/frame_clock_test.c src/frame_clock.h | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) tests/frame_clock_test.c -o $@ -lm

$(TEST_BUILD_DIR)/gem-menu-test: tests/gem_menu_test.c src/gem_menu.c src/gem_menu.h deps/cjson/cJSON.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) tests/gem_menu_test.c src/gem_menu.c deps/cjson/cJSON.c -o $@ -lm

$(TEST_BUILD_DIR)/app-paths-probe: tests/app_paths_probe.c src/app_paths.c src/app_paths.h | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) tests/app_paths_probe.c src/app_paths.c -o $@

ifeq ($(USE_OZZ),1)
$(TEST_BUILD_DIR)/animator.o: src/animator.c src/animator.h src/ozz_bridge.h src/foot_lock.h | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) -DVRM_USE_OZZ=1 -c src/animator.c -o $@

$(TEST_BUILD_DIR)/transition.o: src/transition.c src/transition.h | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) -c src/transition.c -o $@

$(TEST_BUILD_DIR)/pose.o: src/pose.c src/gltf_loader.h | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) -c src/pose.c -o $@

$(TEST_BUILD_DIR)/foot-lock-test: tests/foot_lock_test.cpp src/foot_lock.cpp src/foot_lock.h $(TEST_BUILD_DIR)/pose.o $(OZZ_LIBS)
	$(CXX) $(filter-out -std=c11,$(TEST_CFLAGS)) -std=c++20 -DVRM_USE_OZZ=1 -Ideps/cglm/include -Ideps/ozz-animation/include tests/foot_lock_test.cpp src/foot_lock.cpp $(TEST_BUILD_DIR)/pose.o $(OZZ_LIBS) -o $@ -lm

$(TEST_BUILD_DIR)/animation-transition-test: tests/animation_transition_test.cpp src/ozz_bridge.cpp src/ozz_bridge.h src/foot_lock.cpp src/foot_lock.h $(TEST_BUILD_DIR)/pose.o $(TEST_BUILD_DIR)/animator.o $(TEST_BUILD_DIR)/transition.o $(OZZ_LIBS)
	$(CXX) $(filter-out -std=c11,$(TEST_CFLAGS)) -std=c++20 -DVRM_USE_OZZ=1 -Ideps/cglm/include -Ideps/ozz-animation/include tests/animation_transition_test.cpp src/ozz_bridge.cpp src/foot_lock.cpp $(TEST_BUILD_DIR)/pose.o $(TEST_BUILD_DIR)/animator.o $(TEST_BUILD_DIR)/transition.o $(OZZ_LIBS) -o $@ -lm

OZZ_TEST = $(TEST_BUILD_DIR)/animation-transition-test
FOOT_TEST = $(TEST_BUILD_DIR)/foot-lock-test
endif

test: $(TEST_BUILD_DIR)/app-paths-probe $(TEST_BUILD_DIR)/ipc-server $(TEST_BUILD_DIR)/audio-test $(TEST_BUILD_DIR)/transition-test $(TEST_BUILD_DIR)/spring-test $(TEST_BUILD_DIR)/viseme-default-test $(TEST_BUILD_DIR)/expression-test $(TEST_BUILD_DIR)/anim-config-test $(TEST_BUILD_DIR)/bounds-test $(TEST_BUILD_DIR)/input-mask-test $(TEST_BUILD_DIR)/settings-test $(TEST_BUILD_DIR)/frame-clock-test $(TEST_BUILD_DIR)/gem-menu-test $(OZZ_TEST) $(FOOT_TEST)
	$(if $(OZZ_TEST),$(OZZ_TEST),true)
	$(if $(FOOT_TEST),$(FOOT_TEST),true)
	$(TEST_BUILD_DIR)/audio-test
	$(TEST_BUILD_DIR)/transition-test
	$(TEST_BUILD_DIR)/spring-test
	$(TEST_BUILD_DIR)/viseme-default-test
	$(TEST_BUILD_DIR)/expression-test
	$(TEST_BUILD_DIR)/anim-config-test
	$(TEST_BUILD_DIR)/bounds-test
	$(TEST_BUILD_DIR)/input-mask-test
	$(TEST_BUILD_DIR)/settings-test
	$(TEST_BUILD_DIR)/frame-clock-test
	$(TEST_BUILD_DIR)/gem-menu-test
	VRM_TEST_GEM_MENU_PROBE=$(abspath $(TEST_BUILD_DIR)/gem-menu-test) VRM_TEST_IPC_SERVER=$(abspath $(TEST_BUILD_DIR)/ipc-server) VRM_TEST_PATHS_PROBE=$(abspath $(TEST_BUILD_DIR)/app-paths-probe) $(PYTHON) -m unittest discover -s tests -v

test-sanitize:
	$(MAKE) test TEST_BUILD_DIR=build/tests/sanitize TEST_SANITIZERS=address,undefined

studio: all
	./domovushka --studio

# Requires PySide6, an X11 display, sample model and animation assets.
test-ui: all
	python3 tests/qt_smoke.py

# Optional real TTS check; Prepare voice first, or permit the initial model download.
SPEECH_DEVICE ?= vulkan
test-speech: all
	python3 tests/qt_smoke.py --speech --speech-device $(SPEECH_DEVICE)

# Streamed local API fixture + real renderer and Nano. No paid LLM request.
test-chat: all
	python3 tests/qt_smoke.py --chat --speech-device $(SPEECH_DEVICE)

test-fit: all
	python3 tests/push_fit_smoke.py

test-desktop: all
	python3 tests/desktop_smoke.py

ZOOM_MODEL ?= assets/starter/models/anime.vrm
test-zoom: all
	python3 tests/zoom_smoke.py --model "$(ZOOM_MODEL)"

test-assets: all
	python3 tests/starter_assets_smoke.py

$(TEST_BUILD_DIR)/render-target-test.o: tests/render_target_test.c src/renderer.h src/platform.h src/camera.h src/gltf_loader.h src/hit_capture.h | $(TEST_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -Isrc -c $< -o $@

$(TEST_BUILD_DIR)/render-target-test: $(TEST_BUILD_DIR)/render-target-test.o $(filter-out $(OBJ_DIR)/src/main.o,$(OBJ)) $(if $(filter 1,$(USE_OZZ)),$(OZZ_LIBS))
	$(CXX) $(LDFLAGS) $(TEST_BUILD_DIR)/render-target-test.o $(filter-out $(OBJ_DIR)/src/main.o,$(OBJ)) -o $@ $(LDLIBS)

# Needs an OpenGL/X11 display, but no avatar, speech engine or network.
test-render: $(TEST_BUILD_DIR)/render-target-test
	$(TEST_BUILD_DIR)/render-target-test

$(TEST_BUILD_DIR)/asset-preview.o: tests/asset_preview.c src/renderer.h src/animator.h src/camera.h src/spring_bones.h | $(TEST_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -Isrc -c $< -o $@

$(TEST_BUILD_DIR)/asset-preview: $(TEST_BUILD_DIR)/asset-preview.o $(filter-out $(OBJ_DIR)/src/main.o,$(OBJ)) $(if $(filter 1,$(USE_OZZ)),$(OZZ_LIBS))
	$(CXX) $(LDFLAGS) $(TEST_BUILD_DIR)/asset-preview.o $(filter-out $(OBJ_DIR)/src/main.o,$(OBJ)) -o $@ $(LDLIBS)

$(TEST_BUILD_DIR)/spring-asset-test.o: tests/spring_asset_test.c src/spring_bones.h src/animator.h | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -Isrc -c $< -o $@

$(TEST_BUILD_DIR)/spring-asset-test: $(TEST_BUILD_DIR)/spring-asset-test.o $(filter-out $(OBJ_DIR)/src/main.o,$(OBJ)) $(if $(filter 1,$(USE_OZZ)),$(OZZ_LIBS))
	$(CXX) $(LDFLAGS) $(TEST_BUILD_DIR)/spring-asset-test.o $(filter-out $(OBJ_DIR)/src/main.o,$(OBJ)) -o $@ $(LDLIBS)

# Optional real-avatar check: ten seconds of settling in bind and animated poses.
SPRING_MODEL ?= assets/starter/models/anime.vrm
SPRING_ANIMATION ?= assets/starter/animations/humanoid/idle.vrma
SPRING_TIME ?= 1
test-springs: $(TEST_BUILD_DIR)/spring-asset-test
	$(TEST_BUILD_DIR)/spring-asset-test "$(SPRING_MODEL)"
	$(TEST_BUILD_DIR)/spring-asset-test "$(SPRING_MODEL)" "$(SPRING_ANIMATION)" "$(SPRING_TIME)"

# Standalone native Wayland feasibility probe; no GLFW/X11 or avatar dependencies.
WAYLAND_PROTOCOLS_DIR ?= $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER ?= wayland-scanner
WAYLAND_LAYER_XML = deps/wayland-protocols/wlr-layer-shell-unstable-v1.xml

$(WAYLAND_PROBE_DIR):
	mkdir -p $@

$(WAYLAND_PROBE_DIR)/wlr-layer-shell-client.h: $(WAYLAND_LAYER_XML) | $(WAYLAND_PROBE_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(WAYLAND_PROBE_DIR)/wlr-layer-shell-protocol.c: $(WAYLAND_LAYER_XML) | $(WAYLAND_PROBE_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(WAYLAND_PROBE_DIR)/xdg-shell-protocol.c: $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml | $(WAYLAND_PROBE_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(WAYLAND_PROBE_DIR)/overlay: examples/wayland_overlay/main.c $(WAYLAND_PROBE_DIR)/wlr-layer-shell-client.h $(WAYLAND_PROBE_DIR)/wlr-layer-shell-protocol.c $(WAYLAND_PROBE_DIR)/xdg-shell-protocol.c
	$(CC) -std=c11 -Wall -Wextra -g -O2 -I$(WAYLAND_PROBE_DIR) $(shell pkg-config --cflags wayland-client wayland-egl egl epoxy) $(filter %.c,$^) -o $@ $(shell pkg-config --libs wayland-client wayland-egl egl epoxy)

.PHONY: wayland-probe
wayland-probe: $(WAYLAND_PROBE_DIR)/overlay

$(WAYLAND_PROBE_DIR)/xdg-shell-client.h: $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml | $(WAYLAND_PROBE_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(OBJ_DIR)/src/platform_wayland.o: $(WAYLAND_PROBE_DIR)/wlr-layer-shell-client.h $(WAYLAND_PROBE_DIR)/xdg-shell-client.h
$(OBJ_DIR)/src/platform_wayland.o: INCLUDES += -I$(WAYLAND_PROBE_DIR)

$(WAYLAND_PROBE_DIR)/%-protocol.o: $(WAYLAND_PROBE_DIR)/%-protocol.c
	$(CC) -c $< -o $@ $(shell pkg-config --cflags wayland-client)
