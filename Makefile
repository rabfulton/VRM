# VRM Desktop Overlay Viewer - Makefile (GLFW version)

CC = gcc
CXX = g++
CFLAGS = -std=c99 -Wall -Wextra -g -O2 -MMD -MP
CXXFLAGS = -std=c++20 -Wall -Wextra -g -O2 -MMD -MP
CFLAGS += $(shell pkg-config --cflags glfw3 epoxy)
CXXFLAGS += $(shell pkg-config --cflags glfw3 epoxy)
INCLUDES = -Ideps/cglm/include -Ideps/tinygltf -Ideps/cjson -Ideps/stb -Isrc/ui_library

# Optional ozz/Jolt integration
USE_OZZ_JOLT ?= 1
DEPS_BUILD_DIR = deps/_build
OZZ_BUILD_DIR = $(DEPS_BUILD_DIR)/ozz
JOLT_BUILD_DIR = $(DEPS_BUILD_DIR)/jolt
OZZ_LIBS = $(OZZ_BUILD_DIR)/src/animation/offline/libozz_animation_offline_rd.a \
           $(OZZ_BUILD_DIR)/src/animation/runtime/libozz_animation_rd.a \
           $(OZZ_BUILD_DIR)/src/base/libozz_base_rd.a
JOLT_LIBS = $(JOLT_BUILD_DIR)/libJolt.a

LDFLAGS = $(shell pkg-config --libs glfw3 epoxy) -lm -lstdc++ -lX11 -lXext -lpthread -ldl

ifeq ($(USE_OZZ_JOLT),1)
  INCLUDES += -Ideps/ozz-animation/include -Ideps/JoltPhysics
  CFLAGS += -DVRM_USE_OZZ_JOLT=1
  CXXFLAGS += -DVRM_USE_OZZ_JOLT=1
  LDFLAGS += $(OZZ_LIBS) $(JOLT_LIBS)
endif

C_SRC = src/main.c src/platform.c src/renderer.c src/audio.c src/event.c src/viseme.c src/camera.c src/gltf_loader.c src/ipc.c src/animator.c src/spring_bones.c src/anim_config.c src/settings.c src/debug_ui.c src/ui_library/ui.c src/ui_library/import.c deps/cjson/cJSON.c
CXX_SRC = src/gltf_loader_impl.cpp src/tinygltf_impl.cpp src/anim_phys_capi.cpp
ifeq ($(USE_OZZ_JOLT),1)
  CXX_SRC += src/ozz_bridge.cpp
endif

C_OBJ = $(C_SRC:.c=.o)
CXX_OBJ = $(CXX_SRC:.cpp=.o)
OBJ = $(C_OBJ) $(CXX_OBJ)

TARGET = vrm-overlay

.PHONY: all clean debug

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

ifeq ($(USE_OZZ_JOLT),1)
$(OZZ_LIBS):
	cmake -S deps/ozz-animation -B $(OZZ_BUILD_DIR) -DCMAKE_BUILD_TYPE=RelWithDebInfo -Dozz_build_tools=OFF -Dozz_build_fbx=OFF -Dozz_build_gltf=OFF -Dozz_build_samples=OFF -Dozz_build_howtos=OFF -Dozz_build_tests=OFF
	cmake --build $(OZZ_BUILD_DIR) -j4

$(JOLT_LIBS):
	cmake -S deps/JoltPhysics/Build -B $(JOLT_BUILD_DIR) -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UNIT_TESTS=OFF -DTARGET_HELLO_WORLD=OFF -DTARGET_PERFORMANCE_TEST=OFF -DTARGET_SAMPLES=OFF -DTARGET_VIEWER=OFF -DENABLE_INSTALL=OFF -DENABLE_ALL_WARNINGS=OFF -DCPP_EXCEPTIONS_ENABLED=ON -DCPP_RTTI_ENABLED=ON -DJPH_USE_VK=OFF -DJPH_USE_DX12=OFF -DJPH_USE_MTL=OFF
	cmake --build $(JOLT_BUILD_DIR) -j4

$(TARGET): $(OZZ_LIBS) $(JOLT_LIBS)
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
