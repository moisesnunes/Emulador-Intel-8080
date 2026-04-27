// cpm_ccp.cpp — CP/M 2.2 Console Command Processor (CCP)
//
// Provides an "A>" prompt, built-in commands (DIR, ERA, REN, TYPE, USER),
// and loads arbitrary .COM files into the TPA so they can be executed.
// All I/O goes through TerminalState::putChar / inputQueue, keeping the
// ImGui rendering loop non-blocking.

#include "cpm_ccp.h"
#include <cstring>
#include <cctype>
#include <cstdio>
#include <algorithm>
#include <dirent.h>

// ── Helpers ──────────────────────────────────────────────────────────────────

static void CCPPrint(CPMState& cpm, const char* s) {
    for (; *s; ++s) {
        if (*s == '\n') cpm.terminal.putChar('\r');
        cpm.terminal.putChar(*s);
    }
}

static std::string CCPUpper(std::string s) {
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

// Simple wildcard match: '?' = any char, '*' = any sequence.
static bool CCPMatch(const std::string& name, const std::string& pat) {
    size_t ni = 0, pi = 0, star = std::string::npos, lastN = 0;
    while (ni < name.size()) {
        if (pi < pat.size() && (pat[pi] == '?' || pat[pi] == name[ni])) {
            ni++; pi++;
        } else if (pi < pat.size() && pat[pi] == '*') {
            star = pi++; lastN = ni;
        } else if (star != std::string::npos) {
            pi = star + 1; ni = ++lastN;
        } else {
            return false;
        }
    }
    while (pi < pat.size() && pat[pi] == '*') pi++;
    return pi == pat.size();
}

// Parse "DRIVE:NAME.EXT" into an FCB at fcbAddr.
static void CCPSetupFCB(intel8080* cpu, uint16_t fcbAddr, const std::string& token) {
    memset(&cpu->memory[fcbAddr], 0, 36);
    memset(&cpu->memory[fcbAddr + 1], ' ', 11);
    if (token.empty()) return;

    std::string t = CCPUpper(token);
    size_t i = 0;
    // Drive letter?
    if (t.size() >= 2 && t[1] == ':') {
        cpu->memory[fcbAddr] = (uint8_t)(t[0] - 'A' + 1);
        i = 2;
    }
    std::string rest = t.substr(i);
    auto dot = rest.find('.');
    std::string nm = (dot != std::string::npos) ? rest.substr(0, dot) : rest;
    std::string ex = (dot != std::string::npos) ? rest.substr(dot + 1) : "";
    for (size_t k = 0; k < std::min(nm.size(), (size_t)8); k++)
        cpu->memory[fcbAddr + 1 + k] = (uint8_t)nm[k];
    for (size_t k = 0; k < std::min(ex.size(), (size_t)3); k++)
        cpu->memory[fcbAddr + 9 + k] = (uint8_t)ex[k];
}

// ── Drive resolution helper ───────────────────────────────────────────────────

// Given a token that may start with "D:", returns the host directory for that
// drive and strips the prefix from `token` in-place.
static const std::string& CCPResolveDir(CPMState& cpm, std::string& token) {
    if (token.size() >= 2 && token[1] == ':') {
        int d = token[0] - 'A';
        token = token.substr(2);
        return cpm.driveDir(d);
    }
    return cpm.currentDiskDir();
}

// ── Built-in commands ─────────────────────────────────────────────────────────

static void CCPBuiltinDir(CPMState& cpm, const std::string& args) {
    std::string pat = args.empty() ? "*.*" : CCPUpper(args);
    const std::string& dirPath = CCPResolveDir(cpm, pat);
    if (pat.empty()) pat = "*.*";

    DIR* dir = opendir(dirPath.c_str());
    if (!dir) { CCPPrint(cpm, "No files\n"); return; }

    char driveLetter = (char)('A' + cpm.currentDrive);
    char header[8]; snprintf(header, sizeof(header), "%c: ", driveLetter);
    CCPPrint(cpm, "\n");
    CCPPrint(cpm, header);

    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string uname = CCPUpper(ent->d_name);
        if (!CCPMatch(uname, pat)) continue;

        auto dot = uname.rfind('.');
        std::string nm = (dot != std::string::npos) ? uname.substr(0, dot) : uname;
        std::string ex = (dot != std::string::npos) ? uname.substr(dot + 1) : "   ";
        while (nm.size() < 8) nm += ' ';
        while (ex.size() < 3) ex += ' ';

        char entry[16];
        snprintf(entry, sizeof(entry), "%.8s.%.3s  ", nm.c_str(), ex.c_str());
        if (count > 0 && count % 4 == 0) CCPPrint(cpm, "\n   ");
        CCPPrint(cpm, entry);
        count++;
    }
    closedir(dir);

    if (count == 0) CCPPrint(cpm, "No file");
    CCPPrint(cpm, "\n");
}

static void CCPBuiltinEra(CPMState& cpm, const std::string& args) {
    if (args.empty()) { CCPPrint(cpm, "ERA requires a filename\n"); return; }
    std::string pat = CCPUpper(args);
    const std::string& dirPath = CCPResolveDir(cpm, pat);

    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string uname = CCPUpper(ent->d_name);
        if (CCPMatch(uname, pat)) {
            std::string path = dirPath + "/" + ent->d_name;
            remove(path.c_str());
        }
    }
    closedir(dir);
}

static void CCPBuiltinRen(CPMState& cpm, const std::string& args) {
    // Syntax: REN NEW=OLD  or  REN NEW OLD
    std::string a = CCPUpper(args);
    size_t sep = a.find('=');
    if (sep == std::string::npos) sep = a.find(' ');
    if (sep == std::string::npos) { CCPPrint(cpm, "REN: use NEW=OLD\n"); return; }
    std::string newName = a.substr(0, sep);
    std::string oldName = a.substr(sep + 1);
    while (!oldName.empty() && oldName[0] == ' ') oldName = oldName.substr(1);
    std::string oldPath = cpm.currentDiskDir() + "/" + oldName;
    std::string newPath = cpm.currentDiskDir() + "/" + newName;
    if (rename(oldPath.c_str(), newPath.c_str()) != 0)
        CCPPrint(cpm, "REN failed\n");
}

static void CCPBuiltinType(CPMState& cpm, const std::string& args) {
    if (args.empty()) { CCPPrint(cpm, "TYPE requires a filename\n"); return; }
    std::string filename = CCPUpper(args);
    const std::string& dirPath = CCPResolveDir(cpm, filename);
    std::string path = dirPath + "/" + filename;
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) {
        CCPPrint(cpm, args.c_str());
        CCPPrint(cpm, ": not found\n");
        return;
    }
    CCPPrint(cpm, "\n");
    int ch;
    while ((ch = fgetc(fp)) != EOF) {
        if (ch == 0x1A) break;  // CP/M EOF marker
        if (ch == '\n') cpm.terminal.putChar('\r');
        cpm.terminal.putChar((char)ch);
    }
    fclose(fp);
    CCPPrint(cpm, "\n");
}

static void CCPBuiltinUser(CPMState& cpm, const std::string& args) {
    if (args.empty()) {
        char buf[16]; snprintf(buf, sizeof(buf), "User: %d\n", cpm.currentUser);
        CCPPrint(cpm, buf);
        return;
    }
    int n = atoi(args.c_str());
    if (n >= 0 && n <= 15) cpm.currentUser = (uint8_t)n;
    else CCPPrint(cpm, "User 0-15\n");
}

// ── .COM loader ───────────────────────────────────────────────────────────────

bool CCPLoadCom(intel8080* cpu, CPMState& cpm,
                const std::string& name, const std::string& args) {
    std::string uname = CCPUpper(name);
    const std::string& comDir = CCPResolveDir(cpm, uname);

    // Try NAME.COM, then NAME as-is (might already carry extension).
    std::string path = comDir + "/" + uname + ".COM";
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) {
        path = comDir + "/" + uname;
        fp   = fopen(path.c_str(), "rb");
    }
    if (!fp) {
        CCPPrint(cpm, name.c_str());
        CCPPrint(cpm, "?\n");
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size > (long)(BDOS_ADDR - 0x0100)) {
        fclose(fp);
        CCPPrint(cpm, "Program too large\n");
        return false;
    }

    // Clear TPA, load .COM at 0x0100.
    memset(&cpu->memory[0x0100], 0, BDOS_ADDR - 0x0100);
    fread(&cpu->memory[0x0100], 1, (size_t)size, fp);
    fclose(fp);

    CPMCloseAllFiles(cpm);

    // Command tail at 0x0080: length byte + " " + args.
    std::string tail = args.empty() ? "" : (" " + args);
    size_t tlen = std::min(tail.size(), (size_t)126);
    cpu->memory[0x0080] = (uint8_t)tlen;
    memcpy(&cpu->memory[0x0081], tail.c_str(), tlen);
    if (tlen < 127) cpu->memory[0x0081 + tlen] = 0x0D;

    // FCBs from first two whitespace-separated tokens in args.
    std::string a1, a2;
    size_t sp = args.find(' ');
    if (sp == std::string::npos) {
        a1 = args;
    } else {
        a1 = args.substr(0, sp);
        a2 = args.substr(sp + 1);
        while (!a2.empty() && a2[0] == ' ') a2 = a2.substr(1);
    }
    CCPSetupFCB(cpu, 0x005C, a1);
    CCPSetupFCB(cpu, 0x006C, a2);

    // Reset CPU.
    cpu->PC = 0x0100;
    cpu->SP = BDOS_ADDR - 2;
    cpu->memory[BDOS_ADDR - 2] = 0x00;  // return address → warm-boot (0x0000)
    cpu->memory[BDOS_ADDR - 1] = 0x00;
    cpm.dmaAddress      = 0x0080;
    cpm.lineInputActive = false;

    return true;
}

// ── CCPInit ───────────────────────────────────────────────────────────────────

void CCPInit(intel8080* cpu, CPMState& cpm, const std::string& diskDir) {
    CPMInit(cpu, cpm, diskDir);
    cpm.ccpMode    = true;
    cpm.ccpRunning = true;
}

// ── CCPTick ───────────────────────────────────────────────────────────────────

bool CCPTick(intel8080* cpu, CPMState& cpm) {
    // Display prompt once per command cycle.
    if (!cpm.ccpPrompted) {
        char prompt[6];
        snprintf(prompt, sizeof(prompt), "\r%c>", 'A' + cpm.currentDrive);
        CCPPrint(cpm, prompt);
        cpm.ccpPrompted = true;
        cpm.ccpLine.clear();
    }

    // Drain the input queue one character at a time (non-blocking).
    while (!cpm.terminal.inputQueue.empty()) {
        uint8_t ch = cpm.terminal.inputQueue.front();
        cpm.terminal.inputQueue.erase(cpm.terminal.inputQueue.begin());

        if (ch == 0x0D || ch == 0x0A) {            // Enter
            cpm.terminal.putChar('\r');
            cpm.terminal.putChar('\n');
            std::string line = cpm.ccpLine;
            cpm.ccpLine.clear();
            cpm.ccpPrompted = false;
            if (line.empty()) return false;

            // Split into command + args.
            std::string uline = CCPUpper(line);
            size_t sp = uline.find(' ');
            std::string cmd  = (sp != std::string::npos) ? uline.substr(0, sp) : uline;
            std::string args;
            if (sp != std::string::npos) {
                args = uline.substr(sp + 1);
                while (!args.empty() && args[0] == ' ') args = args.substr(1);
            }

            // Drive change: "B:" "C:" "D:"
            if (cmd.size() == 2 && cmd[1] == ':' && cmd[0] >= 'A' && cmd[0] <= 'D') {
                cpm.currentDrive = (uint8_t)(cmd[0] - 'A');
                return false;
            }
            else if (cmd == "DIR"  || cmd == "LS")  { CCPBuiltinDir(cpm, args);  return false; }
            else if (cmd == "ERA"  || cmd == "DEL") { CCPBuiltinEra(cpm, args);  return false; }
            else if (cmd == "REN"  || cmd == "MV")  { CCPBuiltinRen(cpm, args);  return false; }
            else if (cmd == "TYPE" || cmd == "CAT") { CCPBuiltinType(cpm, args); return false; }
            else if (cmd == "USER")                 { CCPBuiltinUser(cpm, args); return false; }
            else if (cmd == "CLS")  { cpm.terminal.clear();                      return false; }
            else if (cmd == "VER") {
                CCPPrint(cpm, "CP/M 2.2 Emulator\n");
                return false;
            }
            else {
                // Attempt to run as a .COM file.
                return CCPLoadCom(cpu, cpm, cmd, args);
            }

        } else if (ch == 0x08 || ch == 0x7F) {     // Backspace / DEL
            if (!cpm.ccpLine.empty()) {
                cpm.ccpLine.pop_back();
                cpm.terminal.putChar('\b');
                cpm.terminal.putChar(' ');
                cpm.terminal.putChar('\b');
            }
        } else if (ch == 0x15) {                    // ^U — erase line
            for (size_t i = 0; i < cpm.ccpLine.size(); i++) {
                cpm.terminal.putChar('\b');
                cpm.terminal.putChar(' ');
                cpm.terminal.putChar('\b');
            }
            cpm.ccpLine.clear();
        } else if (ch == 0x03) {                    // ^C — redisplay prompt
            CCPPrint(cpm, "\n");
            cpm.ccpLine.clear();
            cpm.ccpPrompted = false;
        } else if (ch >= 0x20 && cpm.ccpLine.size() < 126) {
            char c = (char)toupper((unsigned char)ch);
            cpm.ccpLine += c;
            cpm.terminal.putChar(c);
        }
    }
    return false;
}
