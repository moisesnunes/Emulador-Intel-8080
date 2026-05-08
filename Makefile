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
           zilogZ80.cpp \
           alu.cpp \
           gui.cpp \
           input.cpp \
           game_config.cpp \
           cpm_bios.cpp \
           cpm_ccp.cpp \
           cpm_debug_state.cpp \
           msx_machine.cpp \
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

# ── diagnostic test runner (headless, no GUI) ─────────────────────────────────
DIAG_RUNNER  = tests/diag_runner
DIAG_OBJS    = $(BUILD_DIR)/tests/diag_runner.o \
               $(BUILD_DIR)/intel8080.o \
               $(BUILD_DIR)/zilogZ80.o \
               $(BUILD_DIR)/alu.o \
               $(BUILD_DIR)/game_config.o \
               $(BUILD_DIR)/hexbyte.o

DIAG_COMS    = tests/roms/8080PRE.COM tests/roms/8080EXM.COM
Z80_COMS     = tests/roms/zexdoc.com tests/roms/zexall.com

.PHONY: all clean diag_runner test test-z80 test-z80-all

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

# Build the headless runner
diag_runner: $(DIAG_RUNNER)

$(DIAG_RUNNER): $(DIAG_OBJS)
	$(CXX) $^ -o $@

$(BUILD_DIR)/tests/diag_runner.o: tests/diag_runner.cpp
	@mkdir -p $(BUILD_DIR)/tests
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Run diagnostics (build runner first, then execute each ROM)
test-8080: $(DIAG_RUNNER)
	@echo "=== 8080PRE (sanity) ===" && \
	  $(DIAG_RUNNER) tests/roms/8080PRE.COM 50 || true
	@echo "=== 8080EXM (exerciser) ===" && \
	  $(DIAG_RUNNER) tests/roms/8080EXM.COM 25000 || true

# Z80 exerciser — zexdoc tests documented instructions (~46B cycles)
test-z80: $(DIAG_RUNNER)
	@echo "=== zexdoc (Z80 documented) ===" && \
	  $(DIAG_RUNNER) tests/roms/zexdoc.com z80 50000 || true

# Full Z80 exerciser — zexall includes undocumented instructions (~46B cycles)
test-z80-all: $(DIAG_RUNNER)
	@echo "=== zexall (Z80 all) ===" && \
	  $(DIAG_RUNNER) tests/roms/zexall.com z80 50000 || true

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(DIAG_RUNNER)
