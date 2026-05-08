#ifndef ZILOGZ80_H
#define ZILOGZ80_H

#include "intel8080.h"

enum class Z80Prefix : uint8_t {
    None = 0,
    CB,
    ED,
    DD,
    FD,
};

class zilogZ80 : public intel8080
{
public:
    uint8_t I;
    uint8_t R;
    uint16_t IX;
    uint16_t IY;
    uint8_t IM;
    bool IFF1;
    bool IFF2;
    Z80Prefix prefixState;

    // Shadow registers (EX AF,AF' / EXX)
    uint8_t A_, B_, C_, D_, E_, H_, L_;
    bool sf_, zf_, acf_, pf_, cf_, yf_, xf_, nf_;

    // Undocumented flags: bit 5 (Y) and bit 3 (X) of F
    bool yf, xf;
    bool nf; // N (subtract) flag: set by subtraction ops, cleared by addition ops

    zilogZ80();
    void ResetZ80();
};

void ExecuteZ80OpCode(uint8_t OpCode, zilogZ80 *cpu);

#endif // ZILOGZ80_H
