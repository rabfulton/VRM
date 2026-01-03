# VRM Desktop Overlay Viewer - Makefile

CC = gcc
CXX = g++
CFLAGS = -std=c99 -Wall -Wextra -g -O2 -MMD -MP
CXXFLAGS = -std=c++20 -Wall -Wextra -g -O2 -MMD -MP
CFLAGS += $(shell pkg-config --cflags gtk+-3.0 epoxy)
CXXFLAGS += $(shell pkg-config --cflags gtk+-3.0 epoxy)
INCLUDES = -Ideps/cglm/include -Ideps/tinygltf -Ideps/cjson -Ideps/stb

LDFLAGS = $(shell pkg-config --libs gtk+-3.0 epoxy) -lm -lstdc++

C_SRC = src/main.c src/renderer.c src/camera.c src/gltf_loader.c src/ipc.c src/animator.c deps/cjson/cJSON.c
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
