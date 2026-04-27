// ═══════════════════════════════════════════════════════════════════════════════
// CPMDebugState.cpp - Implementação de métodos de debug
// ═══════════════════════════════════════════════════════════════════════════════

#include "cpmdebugstate.h"
#include "intel8080.h"
#include <cstdio>
#include <cstdlib>

// ═══════════════════════════════════════════════════════════════════════════════
// Log de Instruções
// ═══════════════════════════════════════════════════════════════════════════════

void CPMDebugState::logInstruction(intel8080 *cpu)
{
    if (!cpu)
        return;

    // Obter índice no histórico circular
    int idx = currentInstruction % MAXINSTR;

    // Armazenar informações da instrução
    InstructionLog &log = previousInstructions[idx];
    log.pc = cpu->PC;
    log.opcode = cpu->memory[cpu->PC];
    log.param1 = cpu->memory[cpu->PC + 1];
    log.param2 = cpu->memory[cpu->PC + 2];
    log.timestamp = totalCycles;

    // Armazenar estado dos registradores
    log.A = cpu->A;
    log.B = cpu->B;
    log.C = cpu->C;
    log.D = cpu->D;
    log.E = cpu->E;
    log.H = cpu->H;
    log.L = cpu->L;
    log.SP = cpu->SP;
    log.PC = cpu->PC;
    log.carry = cpu->cf;
    log.zero = cpu->zf;
    log.sign = cpu->sf;
    log.parity = cpu->pf;

    // Avançar índice
    currentInstruction++;
    totalCycles += cpu->cycles;

    // Rastrear máximo
    if (totalInstructions < MAXINSTR)
        totalInstructions++;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Display de Registradores
// ═══════════════════════════════════════════════════════════════════════════════

std::string CPMDebugState::getRegisterDisplay(intel8080 *cpu)
{
    if (!cpu)
        return "CPU not initialized";

    char buffer[512];
    snprintf(buffer, sizeof(buffer),
             "PC:%04X SP:%04X  |  A:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X\n"
             "Flags: %c%c%c%c  |  Drive:%c User:%d DMA:%04X",
             cpu->PC, cpu->SP,
             cpu->A, cpu->B, cpu->C, cpu->D, cpu->E, cpu->H, cpu->L,
             cpu->cf ? 'C' : '-',
             cpu->zf ? 'Z' : '-',
             cpu->sf ? 'S' : '-',
             cpu->pf ? 'P' : '-',
             'A' + currentDrive,
             currentUser,
             dmaAddress);

    return std::string(buffer);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Display de Memória
// ═══════════════════════════════════════════════════════════════════════════════

std::string CPMDebugState::getMemoryDisplay(intel8080 *cpu, uint16_t addr, int lines)
{
    if (!cpu)
        return "CPU not initialized";

    std::string result;
    char buffer[256];

    for (int i = 0; i < lines; i++)
    {
        uint16_t lineAddr = addr + (i * 16);
        snprintf(buffer, sizeof(buffer), "%04X: ", lineAddr);
        result += buffer;

        // 16 bytes por linha em hex
        for (int j = 0; j < 16; j++)
        {
            snprintf(buffer, sizeof(buffer), "%02X ", cpu->memory[lineAddr + j]);
            result += buffer;
        }

        result += " | ";

        // 16 caracteres ASCII
        for (int j = 0; j < 16; j++)
        {
            uint8_t byte = cpu->memory[lineAddr + j];
            char c = (byte >= 0x20 && byte < 0x7F) ? (char)byte : '.';
            result += c;
        }

        result += "\n";
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Display da Stack
// ═══════════════════════════════════════════════════════════════════════════════

std::string CPMDebugState::getStackDisplay(intel8080 *cpu)
{
    if (!cpu)
        return "CPU not initialized";

    std::string result;
    char buffer[256];

    result += "=== STACK ===\n";

    uint16_t sp = cpu->SP;

    // Mostrar 8 valores da stack
    for (int i = 0; i < 8; i++)
    {
        uint16_t addr = sp + (i * 2);
        uint16_t value = (cpu->memory[addr + 1] << 8) | cpu->memory[addr];

        snprintf(buffer, sizeof(buffer), "SP+%d (%04X): %04X (%c%c%c%c)\n",
                 i * 2, addr, value,
                 (value >> 24) & 0xFF,
                 (value >> 16) & 0xFF,
                 (value >> 8) & 0xFF,
                 value & 0xFF);
        result += buffer;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Display de Instruções
// ═══════════════════════════════════════════════════════════════════════════════

std::string CPMDebugState::getInstructionDisplay(int index)
{
    if (index < 0 || index >= totalInstructions)
        return "Invalid instruction index";

    // Converter índice relativo para índice no array circular
    int actualIdx = (currentInstruction - totalInstructions + index) % MAXINSTR;
    if (actualIdx < 0)
        actualIdx += MAXINSTR;

    InstructionLog &log = previousInstructions[actualIdx];

    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "%04X: %02X %02X %02X | A:%02X B:%02X C:%02X D:%02X | SP:%04X %s%s%s%s",
             log.pc, log.opcode, log.param1, log.param2,
             log.A, log.B, log.C, log.D,
             log.SP,
             log.carry ? "C" : "-",
             log.zero ? "Z" : "-",
             log.sign ? "S" : "-",
             log.parity ? "P" : "-");

    return std::string(buffer);
}
