// Headless Intel 8080 diagnostic runner.
// Loads a CP/M .COM at 0x0100 and runs it headlessly, intercepting
// BDOS calls (PC == 0x0005) to print output. Exits when PC reaches
// 0x0000 (warm boot) or the CPU halts.
//
// Usage: diag_runner <rom.COM> [max_millions_of_cycles]

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstdint>
#include "../intel8080.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rom.COM> [max_mcycles]\n", argv[0]);
        return 1;
    }

    const char *romPath = argv[1];
    uint64_t maxMCycles = 2000;
    if (argc >= 3)
        maxMCycles = (uint64_t)atol(argv[2]);

    FILE *f = fopen(romPath, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open: %s\n", romPath);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long romSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom((size_t)romSize);
    fread(rom.data(), 1, (size_t)romSize, f);
    fclose(f);

    intel8080 *cpu = new intel8080();
    cpu->arcadeMode = false;

    // CP/M stubs — caught by PC checks before execution reaches them
    cpu->memory[0x0000] = 0x76; // HLT (warm boot)
    cpu->memory[0x0005] = 0x76; // HLT (BDOS entry)

    // Standard CP/M .COM files load at 0x0100
    for (long i = 0; i < romSize && (0x0100 + i) < 0x10000; i++)
        cpu->memory[0x0100 + i] = rom[(size_t)i];

    cpu->PC = 0x0100;
    cpu->SP = 0xF000;

    const uint64_t maxCycles = maxMCycles * 1'000'000ULL;
    uint64_t totalCycles = 0;
    int exitCode = 0;

    while (!cpu->halted && totalCycles < maxCycles) {
        if (cpu->PC == 0x0000) {
            // Warm-boot vector: program finished normally
            printf("\n");
            break;
        }

        if (cpu->PC == 0x0005) {
            // BDOS call: C = function, DE = argument
            switch (cpu->C) {
                case 2:
                    // Console output: print char in E
                    putchar(cpu->E);
                    fflush(stdout);
                    break;
                case 9: {
                    // Print string at DE until '$'
                    uint16_t addr = ((uint16_t)cpu->D << 8) | cpu->E;
                    while (cpu->memory[addr] != '$')
                        putchar(cpu->memory[addr++]);
                    fflush(stdout);
                    break;
                }
                default:
                    break;
            }
            // Simulate RET: pop the return address the CALL pushed
            uint16_t lo = cpu->memory[cpu->SP];
            uint16_t hi = cpu->memory[(uint16_t)(cpu->SP + 1)];
            cpu->SP += 2;
            cpu->PC = lo | ((uint16_t)hi << 8);
            continue;
        }

        uint8_t op = cpu->memory[cpu->PC];
        ExecuteOpCode(op, cpu);
        totalCycles += OPCODE_CYCLES[op];
    }

    if (cpu->halted)
        fprintf(stderr, "\n[CPU halted after %llu cycles]\n", (unsigned long long)totalCycles);
    else if (totalCycles >= maxCycles) {
        fprintf(stderr, "\n[Timeout after %lluM cycles]\n", (unsigned long long)maxMCycles);
        exitCode = 1;
    }

    delete cpu;
    return exitCode;
}
