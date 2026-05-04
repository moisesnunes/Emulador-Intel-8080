#include "alu.h"
#include "intel8080.h"
// class intel8080;

uint16_t ReadWord(intel8080 *cpu, uint16_t pc)
{
    return (cpu->memory[pc + 1] << 8) | cpu->memory[pc];
}

uint16_t AdvanceWord(intel8080 *const cpu)
{
    uint16_t result = ReadWord(cpu, cpu->PC);
    cpu->PC += 2;
    return result;
}

uint8_t ReadByte(intel8080 *cpu, uint16_t pc)
{
    return cpu->memory[pc];
}

uint8_t AdvanceByte(intel8080 *const cpu)
{
    uint8_t result = ReadByte(cpu, cpu->PC);
    cpu->PC += 1;
    return result;
}

uint8_t ReadMemoryHL(intel8080 *const cpu)
{
    uint16_t address = (cpu->H << 8) | (cpu->L & 0xFF);
    return cpu->memory[address];
}

void WriteMemoryHL(intel8080 *const cpu, uint8_t val)
{
    uint16_t address = (cpu->H << 8) | (cpu->L & 0xFF);
    // cpu->memory[address] = val;
    cpu->WriteMem(address, val);
}

uint16_t ReadRegisterMemory(intel8080 *const cpu, uint8_t a, uint8_t b)
{
    uint16_t registerAddress = (a << 8) | (b & 0xFF);
    return cpu->memory[registerAddress];
}

void WriteRegisterMemory(intel8080 *const cpu, uint16_t memoryAddress, uint8_t val)
{
    // cpu->memory[memoryAddress] = val;
    cpu->WriteMem(memoryAddress, val);
}

uint16_t GetBC(intel8080 *const cpu)
{
    return (cpu->B << 8) | cpu->C;
}

uint16_t GetDE(intel8080 *const cpu)
{
    return (cpu->D << 8) | cpu->E;
}

void SetBC(intel8080 *const cpu, uint16_t value)
{
    cpu->B = value >> 8;
    cpu->C = value & 0xFF;
}

void SetDE(intel8080 *const cpu, uint16_t value)
{
    cpu->D = value >> 8;
    cpu->E = value & 0xFF;
}

void SetHL(intel8080 *const cpu, uint16_t value)
{
    cpu->H = value >> 8;
    cpu->L = value & 0xFF;
}

bool FullCarry(uint8_t a, uint8_t b, bool cf)
{
    uint16_t result = a + b + cf;
    return (result >> 8) & 1;
}

bool HalfCarry(uint8_t a, uint8_t b, bool cf)
{
    uint16_t result = a + b + cf;
    uint16_t carry = result ^ a ^ b;
    return (carry >> 4) & 1;
}

void AddReg(intel8080 *cpu, uint8_t *reg, uint8_t val, bool cf)
{
    uint8_t result = *reg + val + cf;
    cpu->cf = FullCarry(*reg, val, cf);
    cpu->acf = HalfCarry(*reg, val, cf);
    cpu->Set_SZP_Flags(result);
    *reg = result;
}

void SubReg(intel8080 *cpu, uint8_t *reg, uint8_t val, bool cf)
{
    AddReg(cpu, reg, ~val, !cf);
    cpu->cf = !cpu->cf;
}

void INX(intel8080 *cpu, uint8_t *a, uint8_t *b)
{
    uint16_t result = ((*a << 8) | (*b & 0xFF)) + 1;
    *a = (result >> 8) & 0xFF;
    *b = result & 0xFF;
}

void DCX(intel8080 *cpu, uint8_t *rega, uint8_t *regb)
{
    uint16_t result = ((*rega << 8) | (*regb & 0xFF)) - 1;
    *rega = (result >> 8) & 0xFF;
    *regb = result & 0xFF;
}

uint8_t INR(intel8080 *cpu, uint8_t registerVal)
{
    uint8_t result = registerVal + 1;
    cpu->acf = (result & 0xF) == 0;
    cpu->Set_SZP_Flags(result);
    return result;
}

uint8_t DCR(intel8080 *cpu, uint8_t registerVal)
{
    uint8_t result = registerVal - 1;
    cpu->acf = !((result & 0xF) == 0xF);
    cpu->Set_SZP_Flags(result);
    return result;
}

void ANA(intel8080 *cpu, uint8_t *reg, uint8_t val)
{
    uint8_t result = *reg & val;
    cpu->acf = ((*reg | val) >> 3) & 1; // Intel spec: OR of bit 3 of both operands
    cpu->cf = 0;
    cpu->Set_SZP_Flags(result);
    *reg = result;
}

void XRA(intel8080 *cpu, uint8_t *reg, uint8_t val)
{
    uint8_t result = *reg ^ val;
    cpu->acf = 0;
    cpu->Set_SZP_Flags(result);
    cpu->cf = 0;
    *reg = result;
}

void ORA(intel8080 *cpu, uint8_t *reg, uint8_t val)
{
    uint8_t result = *reg | val;
    cpu->Set_SZP_Flags(result);
    cpu->acf = 0;
    cpu->cf = 0;
    *reg = result;
}

void CMP(intel8080 *cpu, uint8_t reg, uint8_t val)
{
    uint8_t tmp = reg;
    SubReg(cpu, &tmp, val, 0);
}

void SHLD(intel8080 *cpu)
{
    uint16_t address = AdvanceWord(cpu);
    // cpu->memory[address] = cpu->L;
    // cpu->memory[address+1] = cpu->H;
    cpu->WriteMem(address, cpu->L);
    cpu->WriteMem(address + 1, cpu->H);
}

void STA(intel8080 *cpu)
{
    uint16_t address = AdvanceWord(cpu);
    // cpu->memory[address] = cpu->A;
    cpu->WriteMem(address, cpu->A);
}

void DAD(intel8080 *cpu, uint8_t rega, uint8_t regb)
{
    uint16_t reagab = (rega << 8) | (regb & 0xFF);
    uint16_t reaghl = (cpu->H << 8) | (cpu->L & 0xFF);
    uint32_t result = reagab + reaghl;
    cpu->cf = (result >> 16) & 0x01;
    cpu->H = (result >> 8) & 0xFF;
    cpu->L = result & 0xFF;
}

void DAD(intel8080 *cpu, uint16_t SP)
{
    uint16_t reaghl = (cpu->H << 8) | (cpu->L & 0xFF);
    uint32_t result = SP + reaghl;
    cpu->cf = (result >> 16) & 0x01;
    cpu->H = (result >> 8) & 0xFF;
    cpu->L = result & 0xFF;
    // std::cout << "added to SP in DAD" << std::endl;
}

void RLC(intel8080 *cpu)
{
    uint16_t result = (cpu->A) << 1;
    result |= ((cpu->A & 0xFF) >> 7);
    cpu->cf = result & 0x01;
    cpu->A = result & 0xFF;
}

void RAL(intel8080 *cpu)
{
    uint16_t result = (cpu->A) << 1;
    result |= cpu->cf;
    cpu->cf = ((cpu->A & 0xFF) >> 7) & 0x01;
    cpu->A = result;
}

void RRC(intel8080 *cpu)
{
    uint16_t result = (cpu->A) >> 1;
    result |= ((cpu->A << 7) & 0x80);
    cpu->cf = (result >> 7) & 0x01;
    cpu->A = result & 0xFF;
}

void RAR(intel8080 *cpu)
{
    uint16_t result = (cpu->A) >> 1;
    result |= (cpu->cf << 7);
    cpu->cf = (cpu->A & 0x01);
    cpu->A = result;
}

void DAA(intel8080 *cpu)
{
    bool cy = cpu->cf;
    uint8_t correction = 0;
    uint8_t lsb = cpu->A & 0x0F;
    uint8_t msb = cpu->A >> 4;

    if (cpu->acf || lsb > 9)
        correction += 0x06;

    if (cpu->cf || msb > 9 || (msb >= 9 && lsb > 9))
    {
        correction += 0x60;
        cy = 1;
    }

    AddReg(cpu, &cpu->A, correction, 0);
    cpu->cf = cy;
}

void LHLD(intel8080 *cpu)
{
    uint16_t address = AdvanceWord(cpu);
    cpu->L = cpu->memory[address];
    cpu->H = cpu->memory[address + 1];
}

void JMP(intel8080 *cpu)
{
    uint16_t address = AdvanceWord(cpu);
    cpu->PC = address;
}

void JNZ(intel8080 *cpu)
{
    if (!cpu->zf)
    {
        JMP(cpu);
    }
    else
    {
        AdvanceWord(cpu);
    }
}

void JNC(intel8080 *cpu)
{
    if (!cpu->cf)
    {
        JMP(cpu);
    }
    else
    {
        AdvanceWord(cpu);
    }
}

void JPO(intel8080 *cpu)
{
    if (!cpu->pf)
    {
        JMP(cpu);
    }
    else
    {
        AdvanceWord(cpu);
    }
}

void JP(intel8080 *cpu)
{
    if (!cpu->sf)
    {
        JMP(cpu);
    }
    else
    {
        AdvanceWord(cpu);
    }
}

void JZ(intel8080 *cpu)
{
    if (cpu->zf)
    {
        JMP(cpu);
    }
    else
    {
        AdvanceWord(cpu);
    }
}

void JC(intel8080 *cpu)
{
    if (cpu->cf)
    {
        JMP(cpu);
    }
    else
    {
        AdvanceWord(cpu);
    }
}

void JPE(intel8080 *cpu)
{
    if (cpu->pf)
    {
        JMP(cpu);
    }
    else
    {
        AdvanceWord(cpu);
    }
}

void JM(intel8080 *cpu)
{
    if (cpu->sf)
    {
        JMP(cpu);
    }
    else
    {
        AdvanceWord(cpu);
    }
}

void RET(intel8080 *cpu)
{
    uint16_t result = (cpu->memory[cpu->SP + 1] << 8) | (cpu->memory[cpu->SP] & 0xFF);
    cpu->PC = result;
    cpu->SP = cpu->SP + 2;
    // std::cout << "added to SP + 2 in RET" << std::endl;
}

// Conditional RET: base cost is 5 cycles (table); add 6 when taken → 11 total.
#define COND_RET(cpu, cond) do { if (cond) { RET(cpu); (cpu)->cycles += 6; (cpu)->cyclesInterrupt += 6; } } while(0)

void RNZ(intel8080 *cpu) { COND_RET(cpu, !cpu->zf); }
void RNC(intel8080 *cpu) { COND_RET(cpu, !cpu->cf); }
void RPO(intel8080 *cpu) { COND_RET(cpu, !cpu->pf); }
void RP (intel8080 *cpu) { COND_RET(cpu, !cpu->sf); }
void RZ (intel8080 *cpu) { COND_RET(cpu,  cpu->zf); }
void RC (intel8080 *cpu) { COND_RET(cpu,  cpu->cf); }
void RPE(intel8080 *cpu) { COND_RET(cpu,  cpu->pf); }
void RM (intel8080 *cpu) { COND_RET(cpu,  cpu->sf); }

#undef COND_RET

void PushRegisterPair(intel8080 *cpu, uint8_t HReg, uint8_t LReg)
{
    // cpu->memory[cpu->SP-1] = HReg;
    // cpu->memory[cpu->SP-2] = LReg;

    cpu->WriteMem(cpu->SP - 1, HReg);
    cpu->WriteMem(cpu->SP - 2, LReg);

    cpu->SP = cpu->SP - 2;
    // std::cout << "subtracted 2 from SP in PushReg" << std::endl;
}

void PopRegisterPair(intel8080 *cpu, uint8_t *HReg, uint8_t *LReg)
{
    *HReg = cpu->memory[cpu->SP + 1];
    *LReg = cpu->memory[cpu->SP];
    cpu->SP = cpu->SP + 2;
    // std::cout << "added to SP +2 in PopReg" << std::endl;
}

void PushPSW(intel8080 *cpu)
{
    uint8_t statusReg = (cpu->sf << 7) | (cpu->zf << 6) | (0 << 5) | (cpu->acf << 4) | (0 << 3) | (cpu->pf << 2) | (1 << 1) | (cpu->cf << 0);
    // cpu->memory[cpu->SP - 1] = cpu->A;
    // cpu->memory[cpu->SP - 2] = statusReg;

    cpu->WriteMem(cpu->SP - 1, cpu->A);
    cpu->WriteMem(cpu->SP - 2, statusReg);

    cpu->SP = cpu->SP - 2;
    // std::cout << "subtracted 2 from SP in PushPSW" << std::endl;
}

void PopPSW(intel8080 *cpu)
{
    uint8_t statusReg = cpu->memory[cpu->SP];
    cpu->A = cpu->memory[cpu->SP + 1];
    cpu->sf = (statusReg >> 7) & 0x01;
    cpu->zf = (statusReg >> 6) & 0x01;
    cpu->acf = (statusReg >> 4) & 0x01;
    cpu->pf = (statusReg >> 2) & 0x01;
    cpu->cf = statusReg & 0x01;
    cpu->SP = cpu->SP + 2;

    // std::cout << "added to SP + 2 in PopPSW" << std::endl;
}

void Call(intel8080 *cpu)
{
    uint16_t nextAddress = cpu->PC + 2; // next command address hopefully, Call is 3 bytes, cycle adds 1 to PC, add 2 to get next instruction
    uint8_t HReg = (nextAddress >> 8) & 0xFF;
    uint8_t LReg = (nextAddress) & 0xFF;
    // cpu->memory[cpu->SP - 1] = HReg;
    // cpu->memory[cpu->SP - 2] = LReg;
    cpu->WriteMem(cpu->SP - 1, HReg);
    cpu->WriteMem(cpu->SP - 2, LReg);
    cpu->SP = cpu->SP - 2;
    cpu->PC = AdvanceWord(cpu);
    // std::cout << "subtracted 2 from SP in Call" << std::endl;
}

// Conditional CALL: base cost is 11 cycles (table); add 6 when taken → 17 total.
#define COND_CALL(cpu, cond) do { \
    if (cond) { Call(cpu); (cpu)->cycles += 6; (cpu)->cyclesInterrupt += 6; } \
    else      { AdvanceWord(cpu); } \
} while(0)

void CNZ(intel8080 *cpu) { COND_CALL(cpu, !cpu->zf); }
void CNC(intel8080 *cpu) { COND_CALL(cpu, !cpu->cf); }
void CPO(intel8080 *cpu) { COND_CALL(cpu, !cpu->pf); }
void CP (intel8080 *cpu) { COND_CALL(cpu, !cpu->sf); }
void CZ (intel8080 *cpu) { COND_CALL(cpu,  cpu->zf); }
void CC (intel8080 *cpu) { COND_CALL(cpu,  cpu->cf); }
void CPE(intel8080 *cpu) { COND_CALL(cpu,  cpu->pf); }
void CM (intel8080 *cpu) { COND_CALL(cpu,  cpu->sf); }

#undef COND_CALL

void RST(intel8080 *cpu, uint8_t N)
{
    uint8_t HReg = (cpu->PC >> 8) & 0xFF;
    uint8_t LReg = cpu->PC & 0xFF;
    // cpu->memory[cpu->SP - 1] = HReg;
    // cpu->memory[cpu->SP - 2] = LReg;
    cpu->WriteMem(cpu->SP - 1, HReg);
    cpu->WriteMem(cpu->SP - 2, LReg);
    cpu->SP = cpu->SP - 2;
    cpu->PC = N * 8;
    // std::cout << "subtracted 2 from SP in RST" << std::endl;
}

void ADI(intel8080 *cpu)
{
    AddReg(cpu, &cpu->A, AdvanceByte(cpu), 0);
}

void ACI(intel8080 *cpu)
{
    AddReg(cpu, &cpu->A, AdvanceByte(cpu), cpu->cf);
}

void SUI(intel8080 *cpu)
{
    SubReg(cpu, &cpu->A, AdvanceByte(cpu), 0);
}

void SBI(intel8080 *cpu)
{
    SubReg(cpu, &cpu->A, AdvanceByte(cpu), cpu->cf);
}

void ANI(intel8080 *cpu)
{
    ANA(cpu, &cpu->A, AdvanceByte(cpu));
}

void ORI(intel8080 *cpu)
{
    ORA(cpu, &cpu->A, AdvanceByte(cpu));
}

void XRI(intel8080 *cpu)
{
    XRA(cpu, &cpu->A, AdvanceByte(cpu));
}

void CPI(intel8080 *cpu)
{
    CMP(cpu, cpu->A, AdvanceByte(cpu));
}

void XTHL(intel8080 *cpu)
{
    uint8_t HReg = cpu->H;
    uint8_t LReg = cpu->L;
    uint8_t SP0 = cpu->memory[cpu->SP];
    uint8_t SP1 = cpu->memory[cpu->SP + 1];
    // cpu->memory[cpu->SP] = LReg;
    // cpu->memory[cpu->SP + 1] = HReg;
    cpu->WriteMem(cpu->SP, LReg);
    cpu->WriteMem(cpu->SP + 1, HReg);
    cpu->L = SP0;
    cpu->H = SP1;
}

void PCHL(intel8080 *cpu)
{
    cpu->PC = (cpu->H << 8) | cpu->L;
}

void SPHL(intel8080 *cpu)
{
    cpu->SP = (cpu->H << 8) | cpu->L;
}

void XCHG(intel8080 *cpu)
{
    uint8_t DReg = cpu->D;
    uint8_t EReg = cpu->E;
    uint8_t HReg = cpu->H;
    uint8_t LReg = cpu->L;
    cpu->D = HReg;
    cpu->E = LReg;
    cpu->H = DReg;
    cpu->L = EReg;
}

void MachineIn(intel8080 *cpu)
{
    uint8_t portNum = AdvanceByte(cpu);
    cpu->A = cpu->IOPorts[portNum];
    switch (portNum)
    {
    case 3:
        cpu->A = (cpu->shiftRegister >> (8 - cpu->shiftOffset)) & 0xFF;
        break;
    }
}

void MachineOut(intel8080 *cpu)
{
    uint8_t portNum = AdvanceByte(cpu);
    // cpu->IOPorts[portNum] = cpu->A;
    switch (portNum)
    {
    case 2:
        cpu->shiftOffset = cpu->A & 0x07;
        break;
    case 4:
        uint16_t mask = 0xFF00;
        uint16_t value = cpu->A << 8;
        cpu->shiftRegister &= !(mask >> cpu->shiftOffset);
        cpu->shiftRegister |= value >> cpu->shiftOffset;
    }
}
