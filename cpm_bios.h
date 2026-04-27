#pragma once
#include "intel8080.h"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

// CP/M 2.2 memory layout (64 KB system):
//   0x0000–0x00FF  Zero page  (warm-boot vector, FCBs, command tail)
//   0x0100–0xF7FF  TPA        (~62 KB for user programs)
//   0xF740         DIRBUF     (128-byte directory scratch buffer)
//   0xF7C0         DPB        (Disk Parameter Block, 15 bytes)
//   0xF7D0         ALV        (allocation vector, dummy)
//   0xF7E0         DPH        (Disk Parameter Header, 16 bytes)
//   0xF800         BDOS entry (RET, intercepted before execution)
static constexpr uint16_t BDOS_ADDR  = 0xF800;
static constexpr uint16_t DIRBUF_ADDR = 0xF740;
static constexpr uint16_t DPB_ADDR    = 0xF7C0;
static constexpr uint16_t ALV_ADDR    = 0xF7D0;
static constexpr uint16_t DPH_ADDR    = 0xF7E0;

// ── Terminal emulation (ADM-3A) ───────────────────────────────────────────────
struct TerminalState {
    static constexpr int COLS = 80;
    static constexpr int ROWS = 24;

    char     buffer[ROWS][COLS];
    int      cursorX = 0;
    int      cursorY = 0;

    enum class EscState { NORMAL, ESC, ESC_EQ, ESC_EQ_ROW };
    EscState escState = EscState::NORMAL;
    int      escRow   = 0;

    std::vector<uint8_t> inputQueue;

    TerminalState();
    void clear();
    void putChar(char ch);
};

// ── CP/M emulator state ───────────────────────────────────────────────────────
struct CPMState {
    uint16_t    dmaAddress   = 0x0080;
    uint8_t     currentDrive = 0;
    uint8_t     currentUser  = 0;
    std::string diskDirs[4]; // A: B: C: D: — each maps to a host subdirectory
    bool        running      = true;

    const std::string& currentDiskDir() const {
        int d = currentDrive < 4 ? currentDrive : 0;
        return diskDirs[d];
    }
    const std::string& driveDir(int n) const {
        int d = (n >= 0 && n < 4) ? n : (currentDrive < 4 ? currentDrive : 0);
        return diskDirs[d];
    }

    TerminalState terminal;

    // fn 10 (Read Console Buffer) — persists across iterations until CR.
    bool        lineInputActive = false;
    uint16_t    lineInputFCB    = 0;
    std::string lineInputAccum;

    // fn 17/18 (Search First/Next).
    std::vector<std::string> searchResults;
    int                      searchIndex = 0;

    // FCB address → open-file slot (avoids storing handle inside FCB byte 16).
    std::map<uint16_t, int> fcbSlotMap;

    // CCP (Console Command Processor) mode.
    // When true, fn 0 / warm-boot return to the CCP instead of exiting.
    bool        ccpMode     = false;
    bool        ccpRunning  = false;  // CCP currently has control
    bool        ccpPrompted = false;  // prompt line already displayed
    std::string ccpLine;              // command being accumulated
};

// Set up zero page, fake BDOS data structures, and CPU registers.
void CPMInit(intel8080* cpu, CPMState& cpm, const std::string& diskDir);

// Close every open file and clear fcbSlotMap. Called when loading a new .COM.
void CPMCloseAllFiles(CPMState& cpm);

// Handle a BDOS call at PC == 0x0005 or PC == BDOS_ADDR.
// Blocking fns (1, 10) return without advancing PC/SP when the queue is empty.
bool BDOSCall(intel8080* cpu, CPMState& cpm);

// Save/load full emulator state (CPU + CPM) to/from a binary file.
bool SaveCPMState(intel8080* cpu, CPMState& cpm, const std::string& path);
bool LoadCPMState(intel8080* cpu, CPMState& cpm, const std::string& path);
