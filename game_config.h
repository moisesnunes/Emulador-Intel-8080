#pragma once
#include <string>
#include <vector>
#include <cstdint>

enum class EmulatorMode {
    ARCADE, // arcade game: bitmap VRAM, scanline interrupts, custom I/O ports
    CPM     // CP/M 2.2: loads a .COM at 0x0100, BDOS calls intercepted at 0x0005
};

enum class TermType {
    ADM3A,    // Lear Siegler ADM-3A + ANSI/VT100 (default)
    IBM3101,  // IBM 3101 ASCII Display Terminal
    VISUAL200 // Visual Technology Visual 200 (VT100-compatible, same as ADM3A)
};

struct RomEntry {
    std::string path;   // path relative to exe dir, with leading /
    int32_t loadAddr;   // -1 = sequential (auto), >= 0 = explicit address
};

struct GameConfig {
    std::string name;
    std::string title;
    std::vector<RomEntry> romFiles;
    EmulatorMode mode = EmulatorMode::ARCADE;
    uint16_t romLoadOffset = 0x0000;  // CP/M .COM files must start at 0x0100
    // ARCADE-only fields:
    int vramStart = 0x2400;
    int vramEnd   = 0x4000;
    int screenW   = 224;
    int screenH   = 256;
    // CP/M peripheral device paths (empty = use defaults)
    std::string cpmReader;
    std::string cpmPunch;
    std::string cpmPrinter;
    // CP/M terminal type (default ADM3A)
    TermType cpmTerminal = TermType::ADM3A;
};

std::string GetExeDir();
GameConfig LoadGameConfig(const std::string& exeDir, const std::string& gameName);
