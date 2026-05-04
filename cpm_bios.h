#pragma once
#include "intel8080.h"
#include "game_config.h"
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Per-FCB search context for fn 17/18 (Search First/Next).
struct SearchContext {
    // (filename, attr): attr bit0 = R/O (FCB ext[0] bit7), bit1 = SYS (FCB ext[1] bit7)
    std::vector<std::pair<std::string, uint8_t>> results;
    int currentIndex = 0;
};

// ── DSK disk image ────────────────────────────────────────────────────────────

// CP/M disk geometry — mirrors the DPB (Disk Parameter Block) fields used by
// the BDOS, plus an optional sector-skew translation table.
struct DskGeometry {
    int spt = 26;   // logical 128-byte sectors per track
    int bsh = 3;    // block shift: block size = (1 << bsh) * 128 bytes
    int dsm = 242;  // highest block number ((dsm+1) blocks total)
    int drm = 63;   // highest directory entry index ((drm+1) entries)
    int off = 2;    // reserved (boot) tracks before the filesystem
    // Logical-to-physical sector translation table (XLT).
    // Empty = image stored in logical order (no skew) — correct for images
    // produced by emulators / cpmtools.  Non-empty = raw physical disk dump.
    std::vector<int> skew;
};

// ── LRU sector cache ──────────────────────────────────────────────────────────
//
// Caches 128-byte sectors from a disk image file.  Sub-sector reads/writes
// (e.g. the 32-byte CP/M directory entries) are handled transparently.
// Write-back: dirty sectors are flushed on eviction, on explicit flush(), and
// before fclose() in DskUnmount.
struct SectorCache {
    static constexpr int SLOTS = 8;

    struct Slot {
        FILE    *fp    = nullptr;
        long     off   = -1;   // 128-byte-aligned byte offset; -1 = empty
        bool     dirty = false;
        uint32_t age   = 0;    // higher = more recently used
        uint8_t  data[128]     = {};
    };

    Slot     slots[SLOTS] = {};
    uint32_t clock        = 0;

    // Find the slot for (fp, sectOff), loading from disk if not present.
    // Evicts the LRU slot when all slots are occupied.
    Slot *pin(FILE *fp, long sectOff) {
        for (auto &s : slots)
            if (s.fp == fp && s.off == sectOff) { s.age = ++clock; return &s; }

        // pick eviction target: empty slot first, then LRU
        Slot *victim = nullptr;
        for (auto &s : slots)
            if (!s.fp) { victim = &s; break; }
        if (!victim) {
            victim = &slots[0];
            for (auto &s : slots)
                if (s.age < victim->age) victim = &s;
        }

        if (victim->dirty && victim->fp) {
            fseek(victim->fp, victim->off, SEEK_SET);
            fwrite(victim->data, 1, 128, victim->fp);
        }

        victim->fp    = fp;
        victim->off   = sectOff;
        victim->dirty = false;
        victim->age   = ++clock;
        fseek(fp, sectOff, SEEK_SET);
        size_t n = fread(victim->data, 1, 128, fp);
        if (n < 128) memset(victim->data + n, 0x1A, 128 - n);
        return victim;
    }

    void read(FILE *fp, long pos, void *buf, int len) {
        Slot *s = pin(fp, pos & ~127L);
        memcpy(buf, s->data + (pos & 127), (size_t)len);
    }

    void write(FILE *fp, long pos, const void *buf, int len) {
        Slot *s = pin(fp, pos & ~127L);
        memcpy(s->data + (pos & 127), buf, (size_t)len);
        s->dirty = true;
    }

    // Flush all dirty slots (or only those belonging to fp when fp != nullptr).
    void flush(FILE *fp = nullptr) {
        for (auto &s : slots) {
            if (!s.dirty || !s.fp) continue;
            if (fp && s.fp != fp) continue;
            fseek(s.fp, s.off, SEEK_SET);
            fwrite(s.data, 1, 128, s.fp);
            fflush(s.fp);
            s.dirty = false;
        }
    }

    // Discard all cached data for fp (or everything when fp == nullptr).
    void invalidate(FILE *fp = nullptr) {
        for (auto &s : slots)
            if (!fp || s.fp == fp) s = Slot{};
    }
};

// Wraps a raw CP/M disk image file (.dsk / .img).
// Geometry defaults match the IBM 3740 8" SD (the CP/M 2.2 reference disk).
struct DskImage {
    FILE*        fp       = nullptr;
    bool         readOnly = false;
    std::string  path;
    SectorCache  cache;

    int spt = 26;
    int bsh = 3;
    int dsm = 242;
    int drm = 63;
    int off = 2;
    std::vector<int> skewTable; // empty = no sector translation

    bool isOpen()         const { return fp != nullptr; }
    int  recsPerBlock()   const { return 1 << bsh; }
    int  bytesPerBlock()  const { return recsPerBlock() * 128; }
    int  dirEntries()     const { return drm + 1; }
    int  blockPtrs()      const { return dsm < 256 ? 16 : 8; }

    // Byte offset of logical record r within block blk, applying sector skew.
    long recByteOff(int blk, int r) const {
        long logSec  = (long)off * spt + (long)blk * recsPerBlock() + r;
        if (skewTable.empty()) return logSec * 128L;
        long track   = logSec / spt;
        int  inTrack = (int)(logSec % spt);
        int  phys    = (inTrack < (int)skewTable.size()) ? skewTable[inTrack] : inTrack;
        return (track * spt + phys) * 128L;
    }
    long dirByteOff(int idx) const {
        return (long)off * spt * 128L + (long)idx * 32L;
    }

    // Cached I/O helpers — use these instead of raw fseek/fread/fwrite on fp.
    void dskRead (long pos, void *buf, int len)        { cache.read (fp, pos, buf, len); }
    void dskWrite(long pos, const void *buf, int len)  { cache.write(fp, pos, buf, len); }
};

// CP/M 2.2 memory layout (64 KB system):
//   0x0000–0x00FF  Zero page  (warm-boot vector, FCBs, command tail)
//   0x0100–0xF7FF  TPA        (~62 KB for user programs)
//   0xF740         DIRBUF     (128-byte directory scratch buffer)
//   0xF7C0         DPB        (Disk Parameter Block, 15 bytes)
//   0xF7D0         ALV        (allocation vector, dummy)
//   0xF7E0         DPH        (Disk Parameter Header, 16 bytes)
//   0xF800         BDOS entry (RET, intercepted before execution)
//   0xF803–0xF842  BIOS stub table (16 entries × 3 bytes, intercepted)
static constexpr uint16_t BDOS_ADDR  = 0xF800;
static constexpr uint16_t BIOS_ADDR  = 0xF803; // WBOOT=+0,CONST=+3,CONIN=+6,CONOUT=+9,...
static constexpr uint16_t DIRBUF_ADDR = 0xF740;
static constexpr uint16_t DPB_ADDR    = 0xF7C0;
static constexpr uint16_t ALV_ADDR    = 0xF7D0;
static constexpr uint16_t DPH_ADDR    = 0xF7E0;

// ── Terminal emulation (ADM-3A + ANSI/VT100) ─────────────────────────────────
struct TerminalState {
    static constexpr int COLS = 80;
    static constexpr int ROWS = 24;

    char     buffer[ROWS][COLS];
    int      cursorX = 0;
    int      cursorY = 0;

    TermType termType = TermType::ADM3A;

    enum class EscState { NORMAL, ESC, ESC_EQ, ESC_EQ_ROW, ESC_BRACKET,
                          ESC_Y, ESC_Y_ROW }; // IBM 3101 direct cursor address
    EscState escState = EscState::NORMAL;
    int      escRow   = 0;

    // ANSI/VT100 CSI parameter accumulation (ESC [ p1 ; p2 ... final)
    static constexpr int MAX_PARAMS = 8;
    int  ansiParams[MAX_PARAMS];
    int  ansiParamCount = 0;
    bool ansiParamPending = false; // digit seen but not yet committed

    std::deque<uint8_t> inputQueue;

    TerminalState();
    void clear();
    void putChar(char ch);
};

static constexpr int MAX_DRIVES = 16; // A: through P:

// ── CP/M emulator state ───────────────────────────────────────────────────────
struct CPMState {
    uint16_t    dmaAddress   = 0x0080;
    uint8_t     currentDrive = 0;
    uint8_t     currentUser  = 0;
    std::string diskDirs[MAX_DRIVES]; // A:–P: — each maps to a host subdirectory
    bool        running      = true;

    const std::string& currentDiskDir() const {
        int d = currentDrive < MAX_DRIVES ? currentDrive : 0;
        return diskDirs[d];
    }
    const std::string& driveDir(int n) const {
        int d = (n >= 0 && n < MAX_DRIVES) ? n : (currentDrive < MAX_DRIVES ? currentDrive : 0);
        return diskDirs[d];
    }

    TerminalState terminal;

    // I/O redirection (< > |) — set by CCP before running a command.
    FILE*       redirectOut     = nullptr; // > file: console output goes here
    bool        pipeCapture     = false;   // |: capture output in pipeBuffer
    std::vector<uint8_t> pipeBuffer;
    std::string pipeStage2Cmd;
    std::string pipeStage2Args;
    std::string pipeStage2OutFile;
    std::string pipeStage2InFile;
    bool        pipeStage2Ready = false;

    // Route a console output byte through redirection / pipe / terminal.
    void consoleOut(char ch) {
        if (redirectOut) { fputc(ch, redirectOut); return; }
        if (pipeCapture) { pipeBuffer.push_back((uint8_t)ch); return; }
        terminal.putChar(ch);
    }

    // fn 10 (Read Console Buffer) — persists across iterations until CR.
    bool        lineInputActive = false;
    uint16_t    lineInputFCB    = 0;
    std::string lineInputAccum;

    // fn 17/18 (Search First/Next) — indexed by FCB address for concurrent contexts.
    std::map<uint16_t, SearchContext> searchContexts;

    // FCB address → open-file slot (avoids storing handle inside FCB byte 16).
    std::map<uint16_t, int> fcbSlotMap;

    // Bitmask of write-protected drives (bit 0 = A:, …, bit 15 = P:).
    uint16_t writeProtectedDrives = 0;

    // Mounted DSK images (nullptr = use host-filesystem mapping in diskDirs[]).
    DskImage* diskImages[MAX_DRIVES] = {};

    // Peripheral I/O device files.
    // Peripheral device paths. Empty = use default (diskDirs[0]/CPM.PUN or CPM.LST).
    // Configurable via game.cfg keys (reader/punch/printer) or CCP commands.
    std::string readerPath;
    std::string punchPath;
    std::string printerPath;
    FILE*       readerFp  = nullptr;
    FILE*       punchFp   = nullptr;
    FILE*       printerFp = nullptr;

    // SUBMIT batch queue: lines injected as if typed at the CCP prompt.
    std::vector<std::string> submitQueue;

    // CCP (Console Command Processor) mode.
    bool        ccpMode     = false;
    bool        ccpRunning  = false;
    bool        ccpPrompted = false;
    std::string ccpLine;

    // ED line-editor state (persists across CCPTick frames).
    bool        editorActive   = false;
    bool        editorModified = false;
    std::string editorFilePath;
    std::string editorCmdBuf;
    std::vector<std::string> editorLines;
};

// Set up zero page, fake BDOS data structures, and CPU registers.
void CPMInit(intel8080* cpu, CPMState& cpm, const std::string& diskDir);

// Close every open file and clear fcbSlotMap. Called when loading a new .COM.
void CPMCloseAllFiles(CPMState& cpm);

// Handle a BDOS call at PC == 0x0005 or PC == BDOS_ADDR.
// Blocking fns (1, 10) return without advancing PC/SP when the queue is empty.
bool BDOSCall(intel8080* cpu, CPMState& cpm);

// Handle a direct BIOS call. PC must be BIOS_ADDR + n*3 (n = function index).
// CONIN (n=2) blocks like BDOS fn 1 when the input queue is empty.
bool BIOSCall(intel8080* cpu, CPMState& cpm);

// Save/load full emulator state (CPU + CPM) to/from a binary file.
bool SaveCPMState(intel8080* cpu, CPMState& cpm, const std::string& path);
bool LoadCPMState(intel8080* cpu, CPMState& cpm, const std::string& path);

// Mount a raw CP/M disk image on drive 0–3 (A:–D:).
// Geometry detection order:
//   1. <hostPath>.geo sidecar file (key=value pairs: spt, bsh, dsm, drm, off, skew)
//   2. Auto-detect from file size (IBM 8" SD and DD are built-in)
//   3. Fallback: IBM 8" SD defaults
// Returns false if the file cannot be opened.
bool DskMount(CPMState& cpm, int drive, const std::string& hostPath, bool readOnly = false);

// Mount with an explicit geometry — bypasses auto-detection entirely.
bool DskMountWithGeometry(CPMState& cpm, int drive, const std::string& hostPath,
                          const DskGeometry& geo, bool readOnly = false);

void DskUnmount(CPMState& cpm, int drive);
void DskUnmountAll(CPMState& cpm);
