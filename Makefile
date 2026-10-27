# VRM Desktop Overlay Viewer - Makefile (GLFW version)

CC = gcc
CXX = g++
CFLAGS = -std=c11 -Wall -Wextra -g -O2 -MMD -MP
CXXFLAGS = -std=c++20 -Wall -Wextra -g -O2 -MMD -MP
CFLAGS += $(shell pkg-config --cflags glfw3 epoxy)
CXXFLAGS += $(shell pkg-config --cflags glfw3 epoxy)
INCLUDES = -Ideps/cglm/include -Ideps/tinygltf -Ideps/cjson -Ideps/stb

# Optional ozz animation sampling and leg IK
USE_OZZ ?= 1
DEPS_BUILD_DIR = deps/_build
OZZ_BUILD_DIR = $(DEPS_BUILD_DIR)/ozz
OZZ_LIBS = $(OZZ_BUILD_DIR)/src/animation/offline/libozz_animation_offline_rd.a \
           $(OZZ_BUILD_DIR)/src/animation/runtime/libozz_animation_rd.a \
           $(OZZ_BUILD_DIR)/src/base/libozz_base_rd.a

LDFLAGS = $(shell pkg-config --libs glfw3 epoxy) -lm -lstdc++ -lX11 -lXext -lpthread -ldl

ifeq ($(USE_OZZ),1)
  INCLUDES += -Ideps/ozz-animation/include
  CFLAGS += -DVRM_USE_OZZ=1
  CXXFLAGS += -DVRM_USE_OZZ=1
  LDFLAGS += $(OZZ_LIBS)
endif

C_SRC = src/main.c src/platform.c src/renderer.c src/audio.c src/event.c src/viseme.c src/camera.c src/gltf_loader.c src/bounds.c src/expressions.c src/pose.c src/ipc.c src/animator.c src/transition.c src/spring_bones.c src/anim_config.c src/settings.c deps/cjson/cJSON.c
CXX_SRC = src/gltf_loader_impl.cpp src/tinygltf_impl.cpp src/foot_lock.cpp
ifeq ($(USE_OZZ),1)
  CXX_SRC += src/ozz_bridge.cpp
endif

C_OBJ = $(C_SRC:.c=.o)
CXX_OBJ = $(CXX_SRC:.cpp=.o)
OBJ = $(C_OBJ) $(CXX_OBJ)

TARGET = vrm-overlay

.PHONY: all clean debug test test-sanitize test-ui test-speech test-chat test-fit studio

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

ifeq ($(USE_OZZ),1)
$(OZZ_LIBS) &:
	cmake -S deps/ozz-animation -B $(OZZ_BUILD_DIR) -DCMAKE_BUILD_TYPE=RelWithDebInfo -Dozz_build_tools=OFF -Dozz_build_fbx=OFF -Dozz_build_gltf=OFF -Dozz_build_samples=OFF -Dozz_build_howtos=OFF -Dozz_build_tests=OFF
	cmake --build $(OZZ_BUILD_DIR) -j4

$(TARGET): $(OZZ_LIBS)
endif

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

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

test: $(TEST_BUILD_DIR)/ipc-server $(TEST_BUILD_DIR)/audio-test $(TEST_BUILD_DIR)/transition-test $(TEST_BUILD_DIR)/spring-test $(TEST_BUILD_DIR)/viseme-default-test $(TEST_BUILD_DIR)/expression-test $(TEST_BUILD_DIR)/anim-config-test $(TEST_BUILD_DIR)/bounds-test $(OZZ_TEST) $(FOOT_TEST)
	$(if $(OZZ_TEST),$(OZZ_TEST),true)
	$(if $(FOOT_TEST),$(FOOT_TEST),true)
	$(TEST_BUILD_DIR)/audio-test
	$(TEST_BUILD_DIR)/transition-test
	$(TEST_BUILD_DIR)/spring-test
	$(TEST_BUILD_DIR)/viseme-default-test
	$(TEST_BUILD_DIR)/expression-test
	$(TEST_BUILD_DIR)/anim-config-test
	$(TEST_BUILD_DIR)/bounds-test
	VRM_TEST_IPC_SERVER=$(abspath $(TEST_BUILD_DIR)/ipc-server) python3 -m unittest discover -s tests -v

test-sanitize:
	$(MAKE) test TEST_BUILD_DIR=build/tests/sanitize TEST_SANITIZERS=address,undefined

studio: all
	python3 control.py

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
