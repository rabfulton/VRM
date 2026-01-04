# VRM Desktop Overlay Viewer - Makefile (GLFW version)

CC = gcc
CXX = g++
CFLAGS = -std=c99 -Wall -Wextra -g -O2 -MMD -MP
CXXFLAGS = -std=c++20 -Wall -Wextra -g -O2 -MMD -MP
CFLAGS += $(shell pkg-config --cflags glfw3 epoxy)
CXXFLAGS += $(shell pkg-config --cflags glfw3 epoxy)
INCLUDES = -Ideps/cglm/include -Ideps/tinygltf -Ideps/cjson -Ideps/stb -Isrc/ui_library

LDFLAGS = $(shell pkg-config --libs glfw3 epoxy) -lm -lstdc++ -lX11 -lXext

C_SRC = src/main.c src/platform.c src/renderer.c src/camera.c src/gltf_loader.c src/ipc.c src/animator.c src/spring_bones.c src/debug_ui.c src/ui_library/ui.c src/ui_library/import.c deps/cjson/cJSON.c
CXX_SRC = src/gltf_loader_impl.cpp

C_OBJ = $(C_SRC:.c=.o)
CXX_OBJ = $(CXX_SRC:.cpp=.o)
OBJ = $(C_OBJ) $(CXX_OBJ)

TARGET = vrm-overlay

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

-include $(OBJ:.o=.d)
