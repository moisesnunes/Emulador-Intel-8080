CXX      = g++
CC       = gcc
CXXFLAGS = -g -I./include/ -I./include/GL/ -I./include/imgui/ -I./
CFLAGS   = -g -I./include/ -I./include/GL/
LDFLAGS  = -lglfw -lGL -ldl -lpthread

TARGET    = Emulator
BUILD_DIR = build

CXX_SRCS = emulador.cpp \
           hexbyte.cpp \
           intel8080.cpp \
           alu.cpp \
           gui.cpp \
           input.cpp \
           game_config.cpp \
           cpm_bios.cpp \
           cpm_ccp.cpp \
           cpmdebugstate.cpp \
           lib/imgui/imgui.cpp \
           lib/imgui/imgui_draw.cpp \
           lib/imgui/imgui_tables.cpp \
           lib/imgui/imgui_widgets.cpp \
           lib/imgui/imgui_demo.cpp \
           lib/imgui/imgui_impl_opengl3.cpp \
           lib/imgui/imgui_impl_glfw.cpp

C_SRCS = src/glad.c

OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CXX_SRCS)) \
       $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRCS))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
