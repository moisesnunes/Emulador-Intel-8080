#include "game_config.h"
#include <fstream>
#include <iostream>
#include <unistd.h>

std::string GetExeDir() {
    char pBuf[256];
    ssize_t bytes = readlink("/proc/self/exe", pBuf, sizeof(pBuf) - 1);
    if (bytes != -1) pBuf[bytes] = '\0';
    std::string exePath(pBuf);
    return exePath.substr(0, exePath.rfind('/'));
}

GameConfig LoadGameConfig(const std::string& exeDir, const std::string& gameName) {
    GameConfig cfg;
    cfg.name  = gameName;
    cfg.title = gameName;

    std::string cfgPath = exeDir + "/roms/" + gameName + "/game.cfg";
    std::ifstream file(cfgPath);
    if (!file.is_open()) {
        std::cout << "Could not open config: " << cfgPath << std::endl;
        return cfg;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            if      (key == "title")     cfg.title      = val;
            else if (key == "mode") {
                cfg.mode = (val == "cpm") ? EmulatorMode::CPM : EmulatorMode::ARCADE;
                if (cfg.mode == EmulatorMode::CPM) cfg.romLoadOffset = 0x0100;
            }
            else if (key == "vramStart")  cfg.vramStart  = std::stoi(val, nullptr, 16);
            else if (key == "vramEnd")    cfg.vramEnd    = std::stoi(val, nullptr, 16);
            else if (key == "screenW")    cfg.screenW    = std::stoi(val);
            else if (key == "screenH")    cfg.screenH    = std::stoi(val);
            else if (key == "reader")     cfg.cpmReader  = val;
            else if (key == "punch")      cfg.cpmPunch   = val;
            else if (key == "printer")    cfg.cpmPrinter = val;
            else if (key == "terminal") {
                if      (val == "ibm3101")  cfg.cpmTerminal = TermType::IBM3101;
                else if (val == "visual200") cfg.cpmTerminal = TermType::VISUAL200;
                else                         cfg.cpmTerminal = TermType::ADM3A;
            }
        } else {
            // filename or filename@0xADDR — explicit address is optional
            RomEntry entry;
            auto at = line.find('@');
            if (at != std::string::npos) {
                entry.path     = "/roms/" + gameName + "/" + line.substr(0, at);
                entry.loadAddr = (int32_t)std::stoi(line.substr(at + 1), nullptr, 16);
            } else {
                entry.path     = "/roms/" + gameName + "/" + line;
                entry.loadAddr = -1;
            }
            cfg.romFiles.push_back(entry);
        }
    }
    return cfg;
}
