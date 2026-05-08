#pragma once
#include <cstdint>
#include <cstring>
#include "intel8080.h"

// ── TMS9918A Video Display Processor ────────────────────────────────────────
// Ports: 0x98 = data, 0x99 = control/status
struct TMS9918A
{
    uint8_t vram[0x4000]; // 16KB VRAM (separate from CPU address space)
    uint8_t regs[8];      // R0–R7
    uint8_t status;       // bit7=VBlank INT, bit6=5th-sprite, bit5=coincidence
    uint16_t addr;        // current 14-bit VRAM address
    uint8_t latch;        // first byte of two-byte control write
    bool latched;         // true = waiting for second control byte
    uint8_t readBuf;      // pre-fetched read byte (VRAM reads are one cycle ahead)

    void Reset();
    void WriteData(uint8_t val);
    void WriteControl(uint8_t val);
    uint8_t ReadData();
    uint8_t ReadStatus();

    // Render a 256×192 frame into rgb (3 bytes per pixel, row-major).
    void RenderFrame(uint8_t *rgb);
};

// ── AY-3-8910 PSG (stub: captures register state, no audio output) ──────────
// Ports: 0xA0 = address latch, 0xA1 = data write, 0xA2 = data read
struct AY8910
{
    uint8_t regs[16] = {};
    uint8_t addrLatch = 0;
};

// ── Intel 8255 PPI ───────────────────────────────────────────────────────────
// Port A (0xA8): slot selection — bits[1:0]=page0 slot, [3:2]=page1, [5:4]=page2, [7:6]=page3
// Port B (0xA9): keyboard matrix data (read, active low)
// Port C (0xAA): keyboard row select (bits[3:0]) + misc output bits
struct PPI8255
{
    uint8_t portA = 0xF0;       // initial: pages 0,1→slot0(BIOS); pages 2,3→slot3(RAM)
    uint8_t portC = 0x00;       // bits[3:0] = keyboard row
    int pageSlot[4] = {0, 0, 3, 3};
};

// ── MSX machine state ────────────────────────────────────────────────────────
struct MSXState
{
    TMS9918A vdp;
    AY8910   psg;
    PPI8255  ppi;

    // Four 64KB slot banks.
    //   Slot 0: BIOS ROM (loaded from msxbios.rom via eprom=)
    //   Slot 3: Main RAM
    //   Slots 1,2: unused (cartridge stubs)
    uint8_t slotRam[4][0x10000];

    // MSX keyboard matrix: 11 rows × 8 columns (bit=0 means pressed, active low).
    // All bits initialised to 1 (no key pressed).
    uint8_t keyMatrix[11];

    bool running = true;
};

// Copy BIOS from cpu->memory (already loaded by eprom=) into slotRam[0],
// set up the initial page→slot mapping and ROM/RAM protection.
void MSXInitMemory(MSXState &msx, intel8080 *cpu);

// Register all MSX I/O port handlers (call ClearPortHandlers first).
// cpu is captured by the PPI slot-swap handler.
void RegisterMSXPorts(MSXState &msx, intel8080 *cpu);

// Translate a GLFW key event into the MSX keyboard matrix.
void MSXKeyCallback(MSXState &msx, int glfwKey, int action);
