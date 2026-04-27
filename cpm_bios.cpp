// cpm_bios.cpp — CP/M 2.2 BDOS emulation layer
//
// Every CALL 0x0005 in the 8080 program is intercepted by BDOSCall(). Console
// I/O goes through TerminalState (the 80×24 virtual ADM-3A terminal) instead
// of stdout/stdin, so everything appears in the ImGui window rendered by
// DrawTerminal() in GUI.cpp.
//
// Blocking functions (fn 1, fn 10) return without advancing PC/SP when the
// input queue is empty. The main loop in 8080Emulator.cpp retries them every
// iteration, rendering GUI frames in between so keyboard events keep flowing.

#include "cpm_bios.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>

// ── FCB field offsets ────────────────────────────────────────────────────────
static constexpr int FCB_DRIVE = 0;
static constexpr int FCB_NAME = 1; // 8 bytes, space-padded, uppercase
static constexpr int FCB_EXT = 9;  // 3 bytes, space-padded, uppercase
static constexpr int FCB_EX = 12;  // extent number (low byte)
static constexpr int FCB_S2 = 14;  // extent number (high byte)
static constexpr int FCB_RC = 15;
static constexpr int FCB_CR = 32; // current record within extent

// ── Open-file table ──────────────────────────────────────────────────────────
static constexpr int MAX_OPEN_FILES = 16;
struct OpenFile
{
    FILE *fp = nullptr;
    bool readOnly = false;
};
static OpenFile openFiles[MAX_OPEN_FILES];

static int AllocFileSlot()
{
    for (int i = 1; i < MAX_OPEN_FILES; i++)
        if (!openFiles[i].fp)
            return i;
    return 0;
}

// ── TerminalState ────────────────────────────────────────────────────────────

TerminalState::TerminalState() { clear(); }

void TerminalState::clear()
{
    for (int r = 0; r < ROWS; r++)
        memset(buffer[r], ' ', COLS);
    cursorX = cursorY = 0;
    escState = EscState::NORMAL;
}

// Write one byte to the terminal, advancing the cursor and interpreting
// ADM-3A escape sequences.  Printable characters are placed directly into
// the grid; control codes move the cursor or erase parts of the screen.
void TerminalState::putChar(char ch)
{
    uint8_t b = (uint8_t)ch;

    // ── Escape sequence continuation ─────────────────────────────────────
    switch (escState)
    {
    case EscState::ESC:
        if (ch == '=')
        {
            escState = EscState::ESC_EQ;
        }
        else if (ch == 'T')
        { // ESC T — erase to end of line
            memset(buffer[cursorY] + cursorX, ' ', COLS - cursorX);
            escState = EscState::NORMAL;
        }
        else if (ch == 'Y')
        { // ESC Y — erase to end of screen
            memset(buffer[cursorY] + cursorX, ' ', COLS - cursorX);
            for (int r = cursorY + 1; r < ROWS; r++)
                memset(buffer[r], ' ', COLS);
            escState = EscState::NORMAL;
        }
        else
        {
            escState = EscState::NORMAL;
        }
        return;
    case EscState::ESC_EQ:
        // "ESC = row" — save the row byte (bias +32) and wait for column.
        escRow = std::max(0, (int)(b - 32));
        escState = EscState::ESC_EQ_ROW;
        return;
    case EscState::ESC_EQ_ROW:
        // "ESC = row col" — move cursor to (col-32, row-32), clamped to grid.
        cursorY = std::min(escRow, ROWS - 1);
        cursorX = std::clamp((int)(b - 32), 0, COLS - 1);
        escState = EscState::NORMAL;
        return;
    default:
        break;
    }

    // ── Control codes ─────────────────────────────────────────────────────
    switch (b)
    {
    case 0x1B: // ESC — start of escape sequence
        escState = EscState::ESC;
        return;
    case 0x0D: // CR — move to start of line
        cursorX = 0;
        return;
    case 0x0A: // LF — move down one line, scroll if at bottom
        cursorY++;
        if (cursorY >= ROWS)
        {
            for (int r = 0; r < ROWS - 1; r++)
                memcpy(buffer[r], buffer[r + 1], COLS);
            memset(buffer[ROWS - 1], ' ', COLS);
            cursorY = ROWS - 1;
        }
        return;
    case 0x08: // BS — cursor left
        if (cursorX > 0)
            cursorX--;
        return;
    case 0x0B: // ^K — cursor up (ADM-3A)
        if (cursorY > 0)
            cursorY--;
        return;
    case 0x18: // ^X — cursor down (WordStar)
        if (cursorY < ROWS - 1)
            cursorY++;
        return;
    case 0x0C: // ^L — form feed / clear screen and home cursor
        clear();
        return;
    case 0x1A: // ^Z — clear screen and home cursor
        clear();
        return;
    case 0x19: // ^Y — erase to end of line
        memset(buffer[cursorY] + cursorX, ' ', COLS - cursorX);
        return;
    case 0x07: // ^G — bell (ignored)
        return;
    default:
        break;
    }

    // ── Printable character ───────────────────────────────────────────────
    if (b >= 0x20 && b < 0x7F)
    {
        buffer[cursorY][cursorX] = ch;
        if (++cursorX >= COLS)
        {
            cursorX = 0;
            cursorY++;
            if (cursorY >= ROWS)
            {
                for (int r = 0; r < ROWS - 1; r++)
                    memcpy(buffer[r], buffer[r + 1], COLS);
                memset(buffer[ROWS - 1], ' ', COLS);
                cursorY = ROWS - 1;
            }
        }
    }
}

// ── FCB helpers ──────────────────────────────────────────────────────────────

static std::string FCBToHostName(intel8080 *cpu, uint16_t fcbAddr)
{
    char name[9] = {}, ext[4] = {};
    for (int i = 0; i < 8; i++)
        name[i] = cpu->memory[fcbAddr + FCB_NAME + i] & 0x7F;
    for (int i = 0; i < 3; i++)
        ext[i] = cpu->memory[fcbAddr + FCB_EXT + i] & 0x7F;
    std::string sName(name), sExt(ext);
    while (!sName.empty() && sName.back() == ' ')
        sName.pop_back();
    while (!sExt.empty() && sExt.back() == ' ')
        sExt.pop_back();
    return sExt.empty() ? sName : sName + "." + sExt;
}

static long FCBFileOffset(intel8080 *cpu, uint16_t fcbAddr)
{
    uint16_t extent = (uint16_t)cpu->memory[fcbAddr + FCB_EX] | ((uint16_t)cpu->memory[fcbAddr + FCB_S2] << 8);
    return ((long)extent * 128 + cpu->memory[fcbAddr + FCB_CR]) * 128;
}

static void FCBAdvanceRecord(intel8080 *cpu, uint16_t fcbAddr)
{
    uint8_t &cr = cpu->memory[fcbAddr + FCB_CR];
    if (++cr >= 128)
    {
        cr = 0;
        if (++cpu->memory[fcbAddr + FCB_EX] >= 32)
        {
            cpu->memory[fcbAddr + FCB_EX] = 0;
            cpu->memory[fcbAddr + FCB_S2]++;
        }
    }
}

// ── CPMInit ──────────────────────────────────────────────────────────────────

void CPMInit(intel8080 *cpu, CPMState &cpm, const std::string &diskDir)
{
    cpm.diskDirs[0] = diskDir;
    cpm.diskDirs[1] = diskDir + "/b";
    cpm.diskDirs[2] = diskDir + "/c";
    cpm.diskDirs[3] = diskDir + "/d";
    for (int i = 1; i < 4; i++)
        mkdir(cpm.diskDirs[i].c_str(), 0755);
    cpm.dmaAddress = 0x0080;
    cpm.currentDrive = 0;
    cpm.currentUser = 0;
    cpm.running = true;
    cpm.ccpMode = false;
    cpm.ccpRunning = false;
    cpm.ccpPrompted = false;
    cpm.ccpLine.clear();
    cpm.fcbSlotMap.clear();

    // 0x0000: JMP 0x0000 — warm-boot vector (program exit returns here)
    cpu->memory[0x0000] = 0xC3;
    cpu->memory[0x0001] = 0x00;
    cpu->memory[0x0002] = 0x00;

    // 0x0005: BDOS entry stub — RET, intercepted before execution.
    cpu->memory[0x0005] = 0xC9;
    // 0x0006-0x0007: BDOS module address (little-endian).
    // Programs read LHLD 0x0006 to determine top of TPA.
    cpu->memory[0x0006] = (uint8_t)(BDOS_ADDR & 0xFF);
    cpu->memory[0x0007] = (uint8_t)(BDOS_ADDR >> 8);

    // RET at BDOS_ADDR catches programs that CALL there directly.
    cpu->memory[BDOS_ADDR] = 0xC9;

    // ── Fake disk data structures ─────────────────────────────────────────
    // DPB (Disk Parameter Block) at DPB_ADDR — standard 8-inch SD parameters.
    uint8_t *dpb = &cpu->memory[DPB_ADDR];
    dpb[0] = 26;
    dpb[1] = 0; // SPT: 26 sectors/track
    dpb[2] = 3; // BSH: block shift (512-byte blocks)
    dpb[3] = 7; // BLM: block mask
    dpb[4] = 0; // EXM: extent mask
    dpb[5] = 242;
    dpb[6] = 0; // DSM: 243 blocks
    dpb[7] = 63;
    dpb[8] = 0;     // DRM: 64 directory entries
    dpb[9] = 0xC0;  // AL0: first 2 blocks reserved
    dpb[10] = 0x00; // AL1
    dpb[11] = 16;
    dpb[12] = 0; // CKS: 16 checksum entries
    dpb[13] = 2;
    dpb[14] = 0; // OFF: 2 reserved tracks

    // DPH (Disk Parameter Header) at DPH_ADDR.
    auto w = [&](uint16_t addr, uint16_t val)
    {
        cpu->memory[addr] = (uint8_t)(val & 0xFF);
        cpu->memory[addr + 1] = (uint8_t)(val >> 8);
    };
    w(DPH_ADDR + 0, 0x0000); // XLT: no translation
    w(DPH_ADDR + 2, 0x0000); // scratch words
    w(DPH_ADDR + 4, 0x0000);
    w(DPH_ADDR + 6, 0x0000);
    w(DPH_ADDR + 8, DIRBUF_ADDR); // DIRBUF
    w(DPH_ADDR + 10, DPB_ADDR);   // DPB
    w(DPH_ADDR + 12, 0x0000);     // CSV: 0 = fixed media
    w(DPH_ADDR + 14, ALV_ADDR);   // ALV: allocation vector

    // ALV: all zeros = all blocks free (we use host filesystem, not blocks).
    memset(&cpu->memory[ALV_ADDR], 0, 32);

    // ── Zero page defaults ────────────────────────────────────────────────
    memset(&cpu->memory[0x005C], 0, 36);
    memset(&cpu->memory[0x005D], ' ', 11);
    memset(&cpu->memory[0x006C], 0, 36);
    memset(&cpu->memory[0x006D], ' ', 11);
    cpu->memory[0x0080] = 0x00; // empty command tail

    cpu->PC = 0x0100;
    cpu->SP = BDOS_ADDR - 2;
    cpu->memory[BDOS_ADDR - 2] = 0x00; // return address → warm-boot vector
    cpu->memory[BDOS_ADDR - 1] = 0x00;
}

void CPMCloseAllFiles(CPMState &cpm)
{
    for (auto &[addr, slot] : cpm.fcbSlotMap)
    {
        if (slot > 0 && slot < MAX_OPEN_FILES && openFiles[slot].fp)
        {
            fclose(openFiles[slot].fp);
            openFiles[slot] = {};
        }
    }
    cpm.fcbSlotMap.clear();
}

// ── BDOS function implementations ────────────────────────────────────────────

static void BDOS_SystemReset(intel8080 * /*cpu*/, CPMState &cpm)
{
    cpm.terminal.putChar('\r');
    cpm.terminal.putChar('\n');
    if (cpm.ccpMode)
    {
        cpm.ccpRunning = true;
        cpm.ccpPrompted = false;
    }
    else
    {
        cpm.running = false;
    }
}

static void BDOS_ConsoleInput(intel8080 *cpu, CPMState &cpm)
{
    // Queue non-empty guaranteed by the early-return guard in BDOSCall.
    uint8_t ch = cpm.terminal.inputQueue.front();
    cpm.terminal.inputQueue.erase(cpm.terminal.inputQueue.begin());
    cpu->A = ch;
    cpm.terminal.putChar((char)ch); // BDOS echoes the character
}

static void BDOS_ConsoleOutput(intel8080 *cpu, CPMState &cpm)
{
    cpm.terminal.putChar((char)cpu->E);
}

// Direct Console I/O (fn 6):
//   E = 0xFF → non-blocking read (returns 0 if queue empty)
//   E = 0xFE → console status (0xFF=ready, 0x00=not ready)
//   E = other → write character
static void BDOS_DirectConsoleIO(intel8080 *cpu, CPMState &cpm)
{
    if (cpu->E == 0xFF)
    {
        if (cpm.terminal.inputQueue.empty())
        {
            cpu->A = 0x00;
        }
        else
        {
            cpu->A = cpm.terminal.inputQueue.front();
            cpm.terminal.inputQueue.erase(cpm.terminal.inputQueue.begin());
        }
    }
    else if (cpu->E == 0xFE)
    {
        cpu->A = cpm.terminal.inputQueue.empty() ? 0x00 : 0xFF;
    }
    else
    {
        cpm.terminal.putChar((char)cpu->E);
    }
}

static void BDOS_PrintString(intel8080 *cpu, CPMState &cpm)
{
    uint16_t addr = ((uint16_t)cpu->D << 8) | cpu->E;
    size_t printed = 0;
    const size_t MAX_PRINT = 4096; // Limite razoável para string impresa

    while (addr < 65536 && cpu->memory[addr] != '$' && printed < MAX_PRINT)
    {
        cpm.terminal.putChar((char)cpu->memory[addr++]);
        printed++;
    }
}

static void BDOS_ConsoleStatus(intel8080 *cpu, CPMState &cpm)
{
    cpu->A = cpm.terminal.inputQueue.empty() ? 0x00 : 0xFF;
}

static void BDOS_Version(intel8080 *cpu, CPMState & /*cpm*/)
{
    cpu->H = 0x00;
    cpu->L = 0x22;
    cpu->A = 0x22; // CP/M 2.2
}

static void BDOS_ResetDisk(intel8080 * /*cpu*/, CPMState &cpm)
{
    cpm.dmaAddress = 0x0080;
    cpm.currentDrive = 0;
}

static void BDOS_SelectDisk(intel8080 *cpu, CPMState &cpm)
{
    cpm.currentDrive = cpu->E & 0x0F;
    cpu->H = cpu->L = cpu->A = 0x00; // success for any drive (all map to diskDir)
}

static void BDOS_OpenFile(intel8080 *cpu, CPMState &cpm)
{
    uint16_t fcbAddr = ((uint16_t)cpu->D << 8) | cpu->E;
    std::string path = cpm.currentDiskDir() + "/" + FCBToHostName(cpu, fcbAddr);
    int slot = AllocFileSlot();
    if (!slot)
    {
        cpu->A = 0xFF;
        return;
    }
    FILE *fp = fopen(path.c_str(), "rb+");
    bool ro = false;
    if (!fp)
    {
        fp = fopen(path.c_str(), "rb");
        ro = true;
    }
    if (!fp)
    {
        cpu->A = 0xFF;
        return;
    }
    openFiles[slot] = {fp, ro};
    cpm.fcbSlotMap[fcbAddr] = slot;
    cpu->memory[fcbAddr + FCB_EX] = cpu->memory[fcbAddr + FCB_CR] = 0;
    cpu->A = 0x00;
}

static void BDOS_CloseFile(intel8080 *cpu, CPMState &cpm)
{
    uint16_t fcbAddr = ((uint16_t)cpu->D << 8) | cpu->E;
    auto it = cpm.fcbSlotMap.find(fcbAddr);
    if (it != cpm.fcbSlotMap.end())
    {
        int slot = it->second;
        if (slot > 0 && slot < MAX_OPEN_FILES && openFiles[slot].fp)
        {
            fclose(openFiles[slot].fp);
            openFiles[slot] = {};
        }
        cpm.fcbSlotMap.erase(it);
    }
    cpu->A = 0x00;
}

// Returns true if `filename` (uppercase, dot-separated) matches the FCB wildcard pattern.
// The FCB name/ext fields use '?' as a single-character wildcard.
static bool FCBMatchesFile(intel8080 *cpu, uint16_t fcbAddr, const std::string &filename)
{
    auto dot = filename.rfind('.');
    std::string namePart = (dot != std::string::npos) ? filename.substr(0, dot) : filename;
    std::string extPart = (dot != std::string::npos) ? filename.substr(dot + 1) : "";
    while (namePart.size() < 8)
        namePart += ' ';
    while (extPart.size() < 3)
        extPart += ' ';
    if (namePart.size() > 8 || extPart.size() > 3)
        return false;
    for (int i = 0; i < 8; i++)
    {
        char p = (char)(cpu->memory[fcbAddr + FCB_NAME + i] & 0x7F);
        if (p == '?')
            continue;
        if ((unsigned char)namePart[i] != (unsigned char)toupper((unsigned char)p))
            return false;
    }
    for (int i = 0; i < 3; i++)
    {
        char p = (char)(cpu->memory[fcbAddr + FCB_EXT + i] & 0x7F);
        if (p == '?')
            continue;
        if ((unsigned char)extPart[i] != (unsigned char)toupper((unsigned char)p))
            return false;
    }
    return true;
}

// Write a fake 32-byte CP/M directory entry for `filename` into the DMA buffer.
static void FillDMAWithEntry(intel8080 *cpu, CPMState &cpm, const std::string &filename)
{
    auto dot = filename.rfind('.');
    std::string namePart = (dot != std::string::npos) ? filename.substr(0, dot) : filename;
    std::string extPart = (dot != std::string::npos) ? filename.substr(dot + 1) : "";
    uint8_t *dma = &cpu->memory[cpm.dmaAddress];
    memset(dma, 0, 32);
    dma[0] = 0x00; // user 0, active
    for (int i = 0; i < 8; i++)
        dma[1 + i] = (i < (int)namePart.size()) ? (uint8_t)namePart[i] : ' ';
    for (int i = 0; i < 3; i++)
        dma[9 + i] = (i < (int)extPart.size()) ? (uint8_t)extPart[i] : ' ';
}

static void BDOS_SearchFirst(intel8080 *cpu, CPMState &cpm)
{
    uint16_t fcbAddr = ((uint16_t)cpu->D << 8) | cpu->E;
    cpm.searchResults.clear();
    cpm.searchIndex = 0;
    DIR *dir = opendir(cpm.currentDiskDir().c_str());
    if (!dir)
    {
        cpu->A = 0xFF;
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr)
    {
        if (ent->d_name[0] == '.')
            continue;
        std::string upper = ent->d_name;
        for (char &c : upper)
            c = (char)toupper((unsigned char)c);
        if (FCBMatchesFile(cpu, fcbAddr, upper))
            cpm.searchResults.push_back(upper);
    }
    closedir(dir);
    if (cpm.searchResults.empty())
    {
        cpu->A = 0xFF;
        return;
    }
    FillDMAWithEntry(cpu, cpm, cpm.searchResults[cpm.searchIndex++]);
    cpu->A = 0x00;
}

static void BDOS_SearchNext(intel8080 *cpu, CPMState &cpm)
{
    if (cpm.searchIndex >= (int)cpm.searchResults.size())
    {
        cpu->A = 0xFF;
        return;
    }
    FillDMAWithEntry(cpu, cpm, cpm.searchResults[cpm.searchIndex++]);
    cpu->A = 0x00;
}

static void BDOS_ReadSequential(intel8080 *cpu, CPMState &cpm)
{
    uint16_t fcbAddr = ((uint16_t)cpu->D << 8) | cpu->E;
    auto it = cpm.fcbSlotMap.find(fcbAddr);
    int slot = (it != cpm.fcbSlotMap.end()) ? it->second : 0;
    if (slot <= 0 || !openFiles[slot].fp)
    {
        cpu->A = 0x09;
        return;
    }
    fseek(openFiles[slot].fp, FCBFileOffset(cpu, fcbAddr), SEEK_SET);
    uint8_t buf[128] = {};
    if (!fread(buf, 1, 128, openFiles[slot].fp))
    {
        cpu->A = 0x01;
        return;
    }
    for (int i = 0; i < 128; i++)
        cpu->memory[cpm.dmaAddress + i] = buf[i];
    FCBAdvanceRecord(cpu, fcbAddr);
    cpu->A = 0x00;
}

static void BDOS_WriteSequential(intel8080 *cpu, CPMState &cpm)
{
    uint16_t fcbAddr = ((uint16_t)cpu->D << 8) | cpu->E;
    auto it = cpm.fcbSlotMap.find(fcbAddr);
    int slot = (it != cpm.fcbSlotMap.end()) ? it->second : 0;
    if (slot <= 0 || !openFiles[slot].fp)
    {
        cpu->A = 0x09;
        return;
    }
    if (openFiles[slot].readOnly)
    {
        cpu->A = 0xFF;
        return;
    }
    fseek(openFiles[slot].fp, FCBFileOffset(cpu, fcbAddr), SEEK_SET);
    if (fwrite(&cpu->memory[cpm.dmaAddress], 1, 128, openFiles[slot].fp) < 128)
    {
        cpu->A = 0x02;
        return;
    }
    fflush(openFiles[slot].fp);
    FCBAdvanceRecord(cpu, fcbAddr);
    cpu->A = 0x00;
}

static void BDOS_MakeFile(intel8080 *cpu, CPMState &cpm)
{
    uint16_t fcbAddr = ((uint16_t)cpu->D << 8) | cpu->E;
    std::string path = cpm.currentDiskDir() + "/" + FCBToHostName(cpu, fcbAddr);
    int slot = AllocFileSlot();
    if (!slot)
    {
        cpu->A = 0xFF;
        return;
    }
    FILE *fp = fopen(path.c_str(), "wb+");
    if (!fp)
    {
        cpu->A = 0xFF;
        return;
    }
    openFiles[slot] = {fp, false};
    cpm.fcbSlotMap[fcbAddr] = slot;
    cpu->memory[fcbAddr + FCB_EX] = cpu->memory[fcbAddr + FCB_CR] = 0;
    cpu->A = 0x00;
}

static void BDOS_EraseFile(intel8080 *cpu, CPMState &cpm)
{
    uint16_t fcbAddr = ((uint16_t)cpu->D << 8) | cpu->E;
    std::string path = cpm.currentDiskDir() + "/" + FCBToHostName(cpu, fcbAddr);
    cpu->A = (remove(path.c_str()) == 0) ? 0x00 : 0xFF;
}

// Rename FCB layout: bytes 1-11 = old name, bytes 17-27 = new name.
static void BDOS_RenameFile(intel8080 *cpu, CPMState &cpm)
{
    uint16_t fcbAddr = ((uint16_t)cpu->D << 8) | cpu->E;
    std::string oldPath = cpm.currentDiskDir() + "/" + FCBToHostName(cpu, fcbAddr);
    std::string newPath = cpm.currentDiskDir() + "/" + FCBToHostName(cpu, fcbAddr + 16);
    cpu->A = (rename(oldPath.c_str(), newPath.c_str()) == 0) ? 0x00 : 0xFF;
}

// Random read: FCB bytes 33-35 (R0/R1/R2) encode the record number.
static void BDOS_ReadRandom(intel8080 *cpu, CPMState &cpm)
{
    uint16_t fcbAddr = ((uint16_t)cpu->D << 8) | cpu->E;
    auto it = cpm.fcbSlotMap.find(fcbAddr);
    int slot = (it != cpm.fcbSlotMap.end()) ? it->second : 0;
    if (slot <= 0 || !openFiles[slot].fp)
    {
        cpu->A = 0x09;
        return;
    }

    uint32_t rec = (uint32_t)cpu->memory[fcbAddr + 33] | ((uint32_t)cpu->memory[fcbAddr + 34] << 8) | ((uint32_t)cpu->memory[fcbAddr + 35] << 16);

    // Usar long long para evitar overflow na multiplicação
    // Máximo seguro: ~4 GB em um arquivo
    long long offset = (long long)rec * 128;
    if (offset < 0 || offset > 0x7FFFFFFF)
    {                  // fseek suporta até ~2GB
        cpu->A = 0x04; // Seek error (código de erro padrão CP/M)
        return;
    }

    fseek(openFiles[slot].fp, (long)offset, SEEK_SET); // ✓ Seguro agora

    uint8_t buf[128] = {};
    if (!fread(buf, 1, 128, openFiles[slot].fp))
    {
        cpu->A = 0x01;
        return;
    }
    for (int i = 0; i < 128; i++)
        cpu->memory[cpm.dmaAddress + i] = buf[i];

    uint16_t extent = rec / 128;
    cpu->memory[fcbAddr + FCB_EX] = (uint8_t)(extent & 0xFF);
    cpu->memory[fcbAddr + FCB_S2] = (uint8_t)(extent >> 8);
    cpu->memory[fcbAddr + FCB_CR] = (uint8_t)(rec % 128);
    cpu->A = 0x00;
}

static void BDOS_WriteRandom(intel8080 *cpu, CPMState &cpm)
{
    uint16_t fcbAddr = ((uint16_t)cpu->D << 8) | cpu->E;
    auto it = cpm.fcbSlotMap.find(fcbAddr);
    int slot = (it != cpm.fcbSlotMap.end()) ? it->second : 0;
    if (slot <= 0 || !openFiles[slot].fp)
    {
        cpu->A = 0x09;
        return;
    }
    if (openFiles[slot].readOnly)
    {
        cpu->A = 0xFF;
        return;
    }
    uint32_t rec = (uint32_t)cpu->memory[fcbAddr + 33] | ((uint32_t)cpu->memory[fcbAddr + 34] << 8) | ((uint32_t)cpu->memory[fcbAddr + 35] << 16);
    fseek(openFiles[slot].fp, rec * 128LL, SEEK_SET);
    if (fwrite(&cpu->memory[cpm.dmaAddress], 1, 128, openFiles[slot].fp) < 128)
    {
        cpu->A = 0x02;
        return;
    }
    fflush(openFiles[slot].fp);
    cpu->memory[fcbAddr + FCB_EX] = (uint8_t)((rec / 128) & 0x1F);
    cpu->memory[fcbAddr + FCB_S2] = (uint8_t)(rec / (128 * 32));
    cpu->memory[fcbAddr + FCB_CR] = (uint8_t)(rec % 128);
    cpu->A = 0x00;
}

// fn 36: Set Random Record — loads R0/R1/R2 from current sequential position (EX/S2/CR).
// Used by linkers and database programs to switch from sequential to random I/O mid-file.
static void BDOS_SetRandomRecord(intel8080 *cpu, CPMState &cpm)
{
    uint16_t fcbAddr = ((uint16_t)cpu->D << 8) | cpu->E;
    uint32_t rec = (uint32_t)cpu->memory[fcbAddr + FCB_S2] * (128u * 32u) + (uint32_t)cpu->memory[fcbAddr + FCB_EX] * 128u + (uint32_t)cpu->memory[fcbAddr + FCB_CR];
    cpu->memory[fcbAddr + 33] = (uint8_t)(rec & 0xFF);
    cpu->memory[fcbAddr + 34] = (uint8_t)((rec >> 8) & 0xFF);
    cpu->memory[fcbAddr + 35] = (uint8_t)((rec >> 16) & 0xFF);
}

static void BDOS_GetCurrentDisk(intel8080 *cpu, CPMState &cpm)
{
    cpu->A = cpm.currentDrive;
}

static void BDOS_SetDMAAddress(intel8080 *cpu, CPMState &cpm)
{
    cpm.dmaAddress = ((uint16_t)cpu->D << 8) | cpu->E;
}

static void BDOS_UserNumber(intel8080 *cpu, CPMState &cpm)
{
    if (cpu->E == 0xFF)
    {
        cpu->A = cpm.currentUser;
    }
    else
    {
        cpm.currentUser = cpu->E & 0x0F;
        cpu->A = 0x00;
    }
}

// ── Missing BDOS stubs ────────────────────────────────────────────────────────
// fn 3: Reader Input — return 0x1A (CP/M EOF, no reader device)
static void BDOS_ReaderInput(intel8080 *cpu, CPMState &) { cpu->A = 0x1A; }
// fn 4: Punch Output — no-op (no punch device)
static void BDOS_PunchOutput(intel8080 *, CPMState &) {}
// fn 5: List Output — no-op (no printer device)
static void BDOS_ListOutput(intel8080 *, CPMState &) {}
// fn 7: Get IOBYTE — always 0 (CON: for all channels)
static void BDOS_GetIOByte(intel8080 *cpu, CPMState &) { cpu->A = 0x00; }
// fn 8: Set IOBYTE — ignored
static void BDOS_SetIOByte(intel8080 *, CPMState &) {}
// fn 24: Write Protect Disk — no-op
static void BDOS_WriteProtectDisk(intel8080 *cpu, CPMState &) { cpu->A = 0x00; }
// fn 27: Get Alloc Address — return HL = ALV_ADDR (dummy allocation vector)
static void BDOS_GetAllocAddr(intel8080 *cpu, CPMState &)
{
    cpu->H = (uint8_t)(ALV_ADDR >> 8);
    cpu->L = (uint8_t)(ALV_ADDR & 0xFF);
}
// fn 29: Get R/O Vector — return HL = 0 (no write-protected drives)
static void BDOS_GetROVector(intel8080 *cpu, CPMState &) { cpu->H = cpu->L = 0x00; }
// fn 30: Set File Attributes — map R/O bit (FCB ext byte 9, bit 7) to host chmod
static void BDOS_SetFileAttribs(intel8080 *cpu, CPMState &cpm)
{
    uint16_t fcbAddr = ((uint16_t)cpu->D << 8) | cpu->E;
    std::string path = cpm.currentDiskDir() + "/" + FCBToHostName(cpu, fcbAddr);
    bool ro = (cpu->memory[fcbAddr + FCB_EXT] & 0x80) != 0;
    chmod(path.c_str(), ro ? 0444 : 0644);
    cpu->A = 0x00;
}
// fn 31: Get Disk Parms — return HL = DPH_ADDR
static void BDOS_GetDiskParms(intel8080 *cpu, CPMState &)
{
    cpu->H = (uint8_t)(DPH_ADDR >> 8);
    cpu->L = (uint8_t)(DPH_ADDR & 0xFF);
}
// fn 37: Reset Drive — no-op (drives don't have physical state)
static void BDOS_ResetDrive(intel8080 *cpu, CPMState &) { cpu->H = cpu->L = cpu->A = 0x00; }

static void BDOS_ComputeFileSize(intel8080 *cpu, CPMState &cpm)
{
    uint16_t fcbAddr = ((uint16_t)cpu->D << 8) | cpu->E;
    std::string path = cpm.currentDiskDir() + "/" + FCBToHostName(cpu, fcbAddr);
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp)
    {
        cpu->A = 0xFF;
        return;
    }
    fseek(fp, 0, SEEK_END);
    uint32_t recs = (uint32_t)((ftell(fp) + 127) / 128);
    fclose(fp);
    cpu->memory[fcbAddr + 33] = (uint8_t)(recs & 0xFF);
    cpu->memory[fcbAddr + 34] = (uint8_t)((recs >> 8) & 0xFF);
    cpu->memory[fcbAddr + 35] = (uint8_t)((recs >> 16) & 0xFF);
    cpu->A = 0x00;
}

// ── Line-input helper (fn 10) ────────────────────────────────────────────────
// Accumulates characters from the terminal input queue, echoing each one, until
// a CR is received. Returns true when the line is complete and the FCB is filled.
static bool HandleLineInput(intel8080 *cpu, CPMState &cpm)
{
    uint16_t bufAddr = ((uint16_t)cpu->D << 8) | cpu->E;

    if (!cpm.lineInputActive)
    {
        cpm.lineInputActive = true;
        cpm.lineInputFCB = bufAddr;
        cpm.lineInputAccum.clear();
    }

    uint8_t maxLen = cpu->memory[cpm.lineInputFCB];

    while (!cpm.terminal.inputQueue.empty())
    {
        uint8_t ch = cpm.terminal.inputQueue.front();
        cpm.terminal.inputQueue.erase(cpm.terminal.inputQueue.begin());

        if (ch == 0x0D)
        {
            // CR — line is complete; write result into the buffer FCB
            size_t len = std::min(cpm.lineInputAccum.size(), (size_t)maxLen);
            cpu->memory[cpm.lineInputFCB + 1] = (uint8_t)len;
            for (size_t i = 0; i < len; i++)
                cpu->memory[cpm.lineInputFCB + 2 + i] = (uint8_t)cpm.lineInputAccum[i];
            cpm.terminal.putChar('\r');
            cpm.terminal.putChar('\n');
            cpm.lineInputActive = false;
            return true;
        }
        else if ((ch == 0x08 || ch == 0x7F) && !cpm.lineInputAccum.empty())
        {
            // Backspace / DEL rubout — erase last character on screen and in accumulator
            cpm.lineInputAccum.pop_back();
            cpm.terminal.putChar('\b');
            cpm.terminal.putChar(' ');
            cpm.terminal.putChar('\b');
        }
        else if (ch >= 0x20 && ch < 0x7F && cpm.lineInputAccum.size() < maxLen)
        {
            cpm.lineInputAccum += (char)ch;
            cpm.terminal.putChar((char)ch); // echo
        }
    }
    return false; // still waiting for CR
}

// ── Simulate RET ─────────────────────────────────────────────────────────────
// Pop the 16-bit return address pushed by CALL 0x0005 and jump back to caller.
static void DoRet(intel8080 *cpu)
{
    uint16_t lo = cpu->memory[cpu->SP];
    uint16_t hi = cpu->memory[cpu->SP + 1];
    cpu->SP += 2;
    cpu->PC = (hi << 8) | lo;
}

// ── BDOSCall — dispatch ───────────────────────────────────────────────────────

bool BDOSCall(intel8080 *cpu, CPMState &cpm)
{
    // ── Blocking: Console Input (fn 1) ───────────────────────────────────
    // If the input queue is empty, return without touching PC/SP. The main
    // loop will call us again next iteration once a key has been queued.
    if (cpu->C == 1 && cpm.terminal.inputQueue.empty())
        return cpm.running;

    // ── Blocking: Read Console Buffer (fn 10) ────────────────────────────
    // HandleLineInput accumulates characters across multiple iterations until
    // the user presses Enter. Same early-return strategy as fn 1.
    if (cpu->C == 10)
    {
        if (!HandleLineInput(cpu, cpm))
            return cpm.running;
        DoRet(cpu);
        return cpm.running;
    }

    // ── Normal dispatch ──────────────────────────────────────────────────
    switch (cpu->C)
    {
    case 0:
        BDOS_SystemReset(cpu, cpm);
        break;
    case 1:
        BDOS_ConsoleInput(cpu, cpm);
        break;
    case 2:
        BDOS_ConsoleOutput(cpu, cpm);
        break;
    case 3:
        BDOS_ReaderInput(cpu, cpm);
        break;
    case 4:
        BDOS_PunchOutput(cpu, cpm);
        break;
    case 5:
        BDOS_ListOutput(cpu, cpm);
        break;
    case 6:
        BDOS_DirectConsoleIO(cpu, cpm);
        break;
    case 7:
        BDOS_GetIOByte(cpu, cpm);
        break;
    case 8:
        BDOS_SetIOByte(cpu, cpm);
        break;
    case 9:
        BDOS_PrintString(cpu, cpm);
        break;
    case 11:
        BDOS_ConsoleStatus(cpu, cpm);
        break;
    case 12:
        BDOS_Version(cpu, cpm);
        break;
    case 13:
        BDOS_ResetDisk(cpu, cpm);
        break;
    case 14:
        BDOS_SelectDisk(cpu, cpm);
        break;
    case 15:
        BDOS_OpenFile(cpu, cpm);
        break;
    case 16:
        BDOS_CloseFile(cpu, cpm);
        break;
    case 17:
        BDOS_SearchFirst(cpu, cpm);
        break;
    case 18:
        BDOS_SearchNext(cpu, cpm);
        break;
    case 19:
        BDOS_EraseFile(cpu, cpm);
        break;
    case 20:
        BDOS_ReadSequential(cpu, cpm);
        break;
    case 21:
        BDOS_WriteSequential(cpu, cpm);
        break;
    case 22:
        BDOS_MakeFile(cpu, cpm);
        break;
    case 23:
        BDOS_RenameFile(cpu, cpm);
        break;
    case 24:
        BDOS_WriteProtectDisk(cpu, cpm);
        break;
    case 25:
        BDOS_GetCurrentDisk(cpu, cpm);
        break;
    case 26:
        BDOS_SetDMAAddress(cpu, cpm);
        break;
    case 27:
        BDOS_GetAllocAddr(cpu, cpm);
        break;
    case 28:
        BDOS_WriteProtectDisk(cpu, cpm);
        break;
    case 29:
        BDOS_GetROVector(cpu, cpm);
        break;
    case 30:
        BDOS_SetFileAttribs(cpu, cpm);
        break;
    case 31:
        BDOS_GetDiskParms(cpu, cpm);
        break;
    case 32:
        BDOS_UserNumber(cpu, cpm);
        break;
    case 33:
        BDOS_ReadRandom(cpu, cpm);
        break;
    case 34:
        BDOS_WriteRandom(cpu, cpm);
        break;
    case 35:
        BDOS_ComputeFileSize(cpu, cpm);
        break;
    case 36:
        BDOS_SetRandomRecord(cpu, cpm);
        break;
    case 37:
        BDOS_ResetDrive(cpu, cpm);
        break;
    default:
        std::cerr << "[BDOS] fn " << (int)cpu->C
                  << " @ PC=0x" << std::hex << cpu->PC << std::dec << "\n";
        cpu->A = cpu->H = cpu->L = 0x00;
        break;
    }

    DoRet(cpu);
    return cpm.running;
}

// ── Save / Load state ─────────────────────────────────────────────────────────

static void writeStr(FILE *fp, const std::string &s)
{
    uint32_t len = (uint32_t)s.size();
    fwrite(&len, 4, 1, fp);
    fwrite(s.data(), 1, len, fp);
}

static std::string readStr(FILE *fp)
{
    uint32_t len = 0;
    fread(&len, 4, 1, fp);
    if (len > 4096)
        return "";
    std::string s(len, '\0');
    fread(s.data(), 1, len, fp);
    return s;
}

bool SaveCPMState(intel8080 *cpu, CPMState &cpm, const std::string &path)
{
    FILE *fp = fopen(path.c_str(), "wb");
    if (!fp)
        return false;

    fwrite("CPM8080\0", 1, 8, fp);
    uint32_t version = 1;
    fwrite(&version, 4, 1, fp);

    // CPU registers
    uint8_t regs[7] = {cpu->A, cpu->B, cpu->C, cpu->D, cpu->E, cpu->H, cpu->L};
    fwrite(regs, 1, 7, fp);
    uint8_t flags[5] = {cpu->sf, cpu->zf, cpu->acf, cpu->pf, cpu->cf};
    fwrite(flags, 1, 5, fp);
    uint8_t misc[2] = {cpu->halted, cpu->interrupts};
    fwrite(misc, 1, 2, fp);
    fwrite(&cpu->cycles, 4, 1, fp);
    fwrite(&cpu->cyclesInterrupt, 4, 1, fp);
    fwrite(cpu->IOPorts, 1, 256, fp);
    fwrite(&cpu->shiftRegister, 2, 1, fp);
    fwrite(&cpu->shiftOffset, 1, 1, fp);
    uint8_t arcade = cpu->arcadeMode ? 1 : 0;
    fwrite(&arcade, 1, 1, fp);
    fwrite(&cpu->SP, 2, 1, fp);
    fwrite(&cpu->PC, 2, 1, fp);
    fwrite(cpu->memory, 1, 0x10000, fp);

    // CPM state
    fwrite(&cpm.dmaAddress, 2, 1, fp);
    fwrite(&cpm.currentDrive, 1, 1, fp);
    fwrite(&cpm.currentUser, 1, 1, fp);
    uint8_t bools[6] = {cpm.running, cpm.ccpMode, cpm.ccpRunning,
                        cpm.ccpPrompted, cpm.lineInputActive, 0};
    fwrite(bools, 1, 6, fp);
    fwrite(&cpm.lineInputFCB, 2, 1, fp);
    int32_t si = cpm.searchIndex;
    fwrite(&si, 4, 1, fp);

    for (int i = 0; i < 4; i++)
        writeStr(fp, cpm.diskDirs[i]);
    writeStr(fp, cpm.ccpLine);
    writeStr(fp, cpm.lineInputAccum);

    fclose(fp);
    return true;
}

bool LoadCPMState(intel8080 *cpu, CPMState &cpm, const std::string &path)
{
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp)
        return false;

    char magic[8];
    fread(magic, 1, 8, fp);
    if (memcmp(magic, "CPM8080", 7) != 0)
    {
        fclose(fp);
        return false;
    }
    uint32_t version;
    fread(&version, 4, 1, fp);
    if (version != 1)
    {
        fclose(fp);
        return false;
    }

    CPMCloseAllFiles(cpm);

    uint8_t regs[7];
    fread(regs, 1, 7, fp);
    cpu->A = regs[0];
    cpu->B = regs[1];
    cpu->C = regs[2];
    cpu->D = regs[3];
    cpu->E = regs[4];
    cpu->H = regs[5];
    cpu->L = regs[6];
    uint8_t flags[5];
    fread(flags, 1, 5, fp);
    cpu->sf = flags[0];
    cpu->zf = flags[1];
    cpu->acf = flags[2];
    cpu->pf = flags[3];
    cpu->cf = flags[4];
    uint8_t misc[2];
    fread(misc, 1, 2, fp);
    cpu->halted = misc[0];
    cpu->interrupts = misc[1];
    fread(&cpu->cycles, 4, 1, fp);
    fread(&cpu->cyclesInterrupt, 4, 1, fp);
    fread(cpu->IOPorts, 1, 256, fp);
    fread(&cpu->shiftRegister, 2, 1, fp);
    fread(&cpu->shiftOffset, 1, 1, fp);
    uint8_t arcade;
    fread(&arcade, 1, 1, fp);
    cpu->arcadeMode = arcade != 0;
    fread(&cpu->SP, 2, 1, fp);
    fread(&cpu->PC, 2, 1, fp);
    fread(cpu->memory, 1, 0x10000, fp);

    fread(&cpm.dmaAddress, 2, 1, fp);
    fread(&cpm.currentDrive, 1, 1, fp);
    fread(&cpm.currentUser, 1, 1, fp);
    uint8_t bools[6];
    fread(bools, 1, 6, fp);
    cpm.running = bools[0];
    cpm.ccpMode = bools[1];
    cpm.ccpRunning = bools[2];
    cpm.ccpPrompted = bools[3];
    cpm.lineInputActive = bools[4];
    fread(&cpm.lineInputFCB, 2, 1, fp);
    int32_t si;
    fread(&si, 4, 1, fp);
    cpm.searchIndex = si;
    cpm.searchResults.clear();
    cpm.fcbSlotMap.clear();

    for (int i = 0; i < 4; i++)
        cpm.diskDirs[i] = readStr(fp);
    cpm.ccpLine = readStr(fp);
    cpm.lineInputAccum = readStr(fp);

    fclose(fp);
    return true;
}
