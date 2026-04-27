#pragma once
#include <string>
#include <vector>
#include <cstdint>

enum class EmulatorMode {
    ARCADE, // arcade game: bitmap VRAM, scanline interrupts, custom I/O ports
    CPM     // CP/M 2.2: loads a .COM at 0x0100, BDOS calls intercepted at 0x0005
};

struct GameConfig {
    std::string name;
    std::string title;
    std::vector<std::string> romFiles; // paths relative to exe dir, with leading /
    EmulatorMode mode = EmulatorMode::ARCADE;
    uint16_t romLoadOffset = 0x0000;  // CP/M .COM files must start at 0x0100
    // ARCADE-only fields:
    int vramStart = 0x2400;
    int vramEnd   = 0x4000;
    int screenW   = 224;
    int screenH   = 256;
};

std::string GetExeDir();
GameConfig LoadGameConfig(const std::string& exeDir, const std::string& gameName);