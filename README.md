# Emulador Intel 8080 / Zilog Z80

Emulador completo de processadores **Intel 8080** e **Zilog Z80** escrito em C++17 com interface gráfica via OpenGL 3.3 + Dear ImGui. Suporta quatro modos de operação distintos: **ARCADE**, **CP/M 2.2**, **Altair 8800** e **MSX**.

---

## Sumário

- [Visão Geral](#visão-geral)
- [Arquitetura do Projeto](#arquitetura-do-projeto)
- [Como Compilar](#como-compilar)
- [Como Executar](#como-executar)
- [Arquivo game.cfg](#arquivo-gamecfg)
- [Núcleo da CPU — Intel 8080](#núcleo-da-cpu--intel-8080)
- [Núcleo da CPU — Zilog Z80](#núcleo-da-cpu--zilog-z80)
- [Modo ARCADE](#modo-arcade)
- [Modo CP/M 2.2](#modo-cpm-22)
- [Modo Altair 8800](#modo-altair-8800)
- [Modo MSX](#modo-msx)
- [Interface Gráfica e Debug](#interface-gráfica-e-debug)
- [Suite de Testes](#suite-de-testes)
- [Dependências Externas](#dependências-externas)
- [Melhorias Possíveis](#melhorias-possíveis)
- [Referências](#referências)

---

## Visão Geral

| Característica       | Detalhes                                                     |
|----------------------|--------------------------------------------------------------|
| CPUs emuladas        | Intel 8080 (1974) e Zilog Z80 (1976)                         |
| Clock (Arcade)       | Configurável via `game.cfg` — padrão 2 MHz a 60 FPS          |
| Clock (MSX)          | Z80 a 3,58 MHz (NTSC) ou 3,55 MHz (PAL)                     |
| Memória              | 64 KB flat (0x0000–0xFFFF) por modo                          |
| Gráficos             | OpenGL 3.3 Core Profile via GLFW + GLAD                      |
| UI de debug          | Dear ImGui com editor hexadecimal de memória                  |
| Jogos Arcade         | Space Invaders, Alien Invaders, Searthie e outros            |
| Ambiente CP/M        | CP/M 2.2 com BDOS, CCP, terminal ADM-3A/VT100/IBM3101        |
| Ambiente Altair      | Altair 8800 com MITS BASIC 4.0 via porta serial              |
| MSX                  | MSX1 com TMS9918A VDP + AY-3-8910 PSG + 8255 PPI            |
| Software CP/M        | Zork, WordStar, dBase II, BDS-C Compiler, NVEDIT e outros   |
| Linguagem            | C++17 (g++)                                                  |
| Plataforma           | Linux (testado em Ubuntu/Debian x86-64)                      |

---

## Arquitetura do Projeto

```
Emulador-Intel-8080/
├── emulador.cpp          # Ponto de entrada — loop principal, timing, despacho de modo
├── intel8080.h / .cpp    # CPU 8080: registradores, memória, tabela de ciclos, opcodes
├── zilogZ80.h / .cpp     # CPU Z80: registradores shadow, índices IX/IY, prefixos CB/DD/ED/FD
├── alu.h / .cpp          # ALU: ADD, SUB, ADC, SBB, AND, OR, XOR, CMP, rotações, DAA
├── gui.h / .cpp          # Janela OpenGL, renderização de VRAM, frames ImGui
├── input.cpp             # Mapeamento de teclado → portas I/O (modo Arcade e MSX)
├── game_config.h / .cpp  # Parser do arquivo game.cfg — detecta modo, CPU, ROMs, periféricos
├── hexbyte.h / .cpp      # Conversão entre string hex e inteiro
│
├── cpm_bios.h / .cpp     # BDOS, BIOS, terminal ADM-3A/VT100/IBM3101, discos, porta serial
├── cpm_ccp.h / .cpp      # Console Command Processor (shell CP/M com pipe, redirecionamento)
├── cpm_debug_state.h / .cpp # Estado de debug para CP/M: histórico, NVRAM, stuck detection
│
├── msx_machine.h / .cpp  # Máquina MSX: TMS9918A VDP, AY-3-8910 PSG, 8255 PPI, slots
│
├── Makefile              # Build system com targets: all, diag_runner, test-8080, test-z80
├── tests/                # Suite de diagnóstico headless (8080PRE, 8080EXM, zexdoc, zexall)
│
└── roms/                 # Um subdiretório por jogo/sistema, cada um com game.cfg
    ├── invaders/         # Space Invaders (invaders.h/g/f/e @ 0x0000–0x1FFF)
    ├── cpm/              # CP/M 2.2 genérico (inicia no CCP, Z80 opcional)
    ├── zork/             # Zork I via CP/M
    ├── wordstar/         # WordStar 3.3 via CP/M
    ├── altair/           # Altair 8800 + MITS BASIC 4.0 (eprom=0x0000,4kbas40.bin)
    ├── msx/              # MSX1 com C-BIOS (cbios_main_msx1.rom)
    └── ...               # dBase II, BDS-C, NVEDIT, datastar, wskpro33, etc.
```

### Fluxo de Dados

```
argv[1] (nome do jogo)
       │
       ▼
  LoadGameConfig()  ←── roms/<jogo>/game.cfg ──→ modo, CPU, ROMs, VRAM, periféricos
       │
       ├── Seleciona intel8080 ou zilogZ80 (herda de intel8080)
       │
       ▼
  cpu->memory[64KB]  ←── LoadRomFile() / eprom= / CCPInit()
       │
       ├── [ARCADE]  → 60 FPS: Execute → ISR scanline → DrawScreen()
       ├── [CP/M]    → CCPTick / BDOSCall / BIOSCall → ImGUIFrameCPM()
       ├── [ALTAIR]  → stdin/stdout raw terminal → loop sem GUI
       └── [MSX]     → Z80 + VBlank IRQ → TMS9918A RenderFrame() → OpenGL
```

---

## Como Compilar

### Requisitos

```bash
sudo apt update
sudo apt install build-essential libglfw3-dev libgl-dev
```

| Dependência      | Onde obter                                    |
|------------------|-----------------------------------------------|
| `g++` (C++17)    | `build-essential`                             |
| `libglfw3-dev`   | APT — única dependência externa               |
| `libgl-dev`      | Mesa ou driver proprietário                   |
| ImGui, GLAD, GLM | **Incluídos** no repositório (`lib/`, `include/`) |

### Compilação

```bash
# Compilar o emulador (executável: ./Emulator)
make

# Compilar apenas o runner de diagnóstico (sem GUI)
make diag_runner

# Limpar artefatos
make clean
```

### Flags de Compilação

| Flag                   | Finalidade                                      |
|------------------------|-------------------------------------------------|
| `-g`                   | Símbolos de debug (GDB/Valgrind)                |
| `-I./include/`         | Headers de GLFW, GLAD, GLM, KHR                 |
| `-I./include/imgui/`   | Dear ImGui                                      |
| `-I./`                 | Headers locais do projeto                       |
| `-lglfw`               | GLFW (janela e input)                           |
| `-lGL`                 | OpenGL                                          |
| `-ldl`                 | `dlopen` (necessário para o loader GLAD)        |
| `-lpthread`            | Threads POSIX                                   |

---

## Como Executar

### Sintaxe geral

```bash
./Emulator <nome-do-jogo> [z80|8080]
```

O segundo argumento sobrepõe o campo `cpu=` do `game.cfg`.

### Exemplos — Jogos Arcade

```bash
# Space Invaders (padrão quando nenhum argumento é passado)
./Emulator
./Emulator invaders

# Alien Invaders
./Emulator alieninv

# Searthie (clone de Space Invaders)
./Emulator searthie
```

### Exemplos — CP/M 2.2

```bash
# Shell CP/M genérico (CCP interativo)
./Emulator cpm

# Forçar Z80 no modo CP/M (override da linha de comando)
./Emulator cpm z80

# Zork I (aventura de texto)
./Emulator zork

# WordStar 3.3 (processador de texto)
./Emulator wordstar

# dBase II
./Emulator dBase_II

# BDS-C Compiler
./Emulator bds-c

# NVEDIT (editor de texto leve)
./Emulator nv-edit

# Jogos CP/M (Chess, Pacman, Tetris, etc.)
./Emulator GAMES
```

### Exemplos — Altair 8800

```bash
# Altair 8800 com MITS BASIC 4.0 (modo console — stdin/stdout)
./Emulator altair
```

O Altair roda em modo **headless serial**: stdin e stdout se tornam o terminal do BASIC. Não abre janela gráfica.

### Exemplos — MSX

```bash
# MSX1 com C-BIOS (requer cbios_main_msx1.rom em roms/msx/)
./Emulator msx
```

---

## Arquivo game.cfg

Cada jogo/sistema tem seu subdiretório em `roms/<nome>/` com um arquivo `game.cfg`. O emulador lê esse arquivo automaticamente.

### Referência completa de chaves

| Chave               | Valores possíveis                           | Descrição                                                   |
|---------------------|---------------------------------------------|-------------------------------------------------------------|
| `title`             | string                                      | Título da janela                                            |
| `mode`              | `arcade` `cpm` `altair` `msx`               | Modo de emulação                                            |
| `cpu`               | `8080` `z80`                                | CPU a usar (Z80 é automático no modo MSX)                   |
| `interrupt_hz`      | float (ex: `60`)                            | Frequência de interrupção de scanline                       |
| `cpu_hz`            | inteiro (ex: `2000000`)                     | Frequência do clock da CPU em Hz                            |
| `scanlines`         | inteiro (ex: `256`)                         | Número de scanlines por frame                               |
| `rst_mid`           | hex (ex: `0x08`)                            | Vetor RST do interrupt de meia tela                         |
| `rst_end`           | hex (ex: `0x10`)                            | Vetor RST do interrupt de fim de tela                       |
| `vramStart`         | hex (ex: `0x2400`)                          | Início da VRAM no espaço de memória                         |
| `vramEnd`           | hex (ex: `0x4000`)                          | Fim da VRAM                                                 |
| `screenW`           | inteiro (ex: `224`)                         | Largura da tela em pixels                                   |
| `screenH`           | inteiro (ex: `256`)                         | Altura da tela em pixels                                    |
| `eprom=ADDR,arq`    | ex: `eprom=0x0000,bios.bin`                 | Carrega `arq` em `ADDR` como ROM somente-leitura            |
| `pc_start`          | hex (ex: `0xE000`)                          | PC inicial (sobrepõe o padrão 0x0000/0x0100)                |
| `terminal`          | `adm3a` `ibm3101` `visual200`               | Tipo de terminal para o modo CP/M                           |
| `serial_port`       | inteiro (ex: `5000`)                        | Porta TCP para serial simulada                              |
| `serial_baud`       | inteiro (ex: `9600`)                        | Baud rate da serial                                         |
| `serial_fifo_path`  | path (ex: `/tmp/cpm-serial`)                | Named pipe para serial bidirecional                         |
| `serial_console`    | `yes` / `no`                                | Modo console: stdin/stdout como terminal serial             |
| `reader`            | path                                        | Dispositivo de leitura CP/M (BDOS fn 3)                     |
| `punch`             | path                                        | Dispositivo de punch CP/M (BDOS fn 4)                       |
| `printer`           | path                                        | Impressora CP/M (BDOS fn 5)                                 |
| `overlay_base`      | hex                                         | Base da região de overlay na TPA                            |
| `overlay_size`      | hex                                         | Tamanho da região de overlay (0 = até o BDOS)               |

### Exemplos de game.cfg

**Space Invaders (Arcade, carregamento com endereço explícito):**
```ini
title = Space Invaders
invaders.h@0x0000
invaders.g@0x0800
invaders.f@0x1000
invaders.e@0x1800
```

**CP/M genérico com serial TCP e Z80:**
```ini
mode=cpm
cpu=z80
title=Comandos Transientes
serial_port=5000
serial_fifo_path=/tmp/cpm-serial
serial_baud=9600
```

**Altair 8800 em modo console:**
```ini
title=Altair 8800 — MITS BASIC 4.0
mode=altair
eprom=0x0000,4kbas40.bin
serial_console=yes
```

**MSX com C-BIOS (NTSC 60 Hz):**
```ini
title=MSX Computer
mode=msx
interrupt_hz=60
cpu_hz=3579545
eprom=0x0000,cbios_main_msx1.rom
```

**Jogo CP/M com arquivo .COM específico:**
```ini
mode=cpm
title=Zork Game
ZORK1.COM
```

**Arcade personalizado com timing diferente:**
```ini
title=Meu Jogo Arcade
mode=arcade
cpu_hz=1789773
interrupt_hz=60
scanlines=262
rst_mid=0x08
rst_end=0x10
vramStart=0x2400
vramEnd=0x4000
screenW=256
screenH=240
mygame.rom
```

---

## Núcleo da CPU — Intel 8080

### Registradores

```
┌──────────────────────────────────────────────────────────┐
│  A  (Acumulador, 8 bits)   │  FLAGS: S Z _ AC _ P _ C   │
├────────────────────────────┴───────────────────────────────┤
│  B        │  C      │  D        │  E      │  H        │  L │
│  (par BC) │         │  (par DE) │         │  (par HL) │    │
├────────────────────────────────────────────────────────────┤
│  SP (Stack Pointer, 16 bits)  │  PC (Program Counter, 16b) │
└────────────────────────────────────────────────────────────┘
```

| Registrador | Bits | Descrição                              |
|-------------|------|----------------------------------------|
| `A`         | 8    | Acumulador — operand implícito da ALU   |
| `B`, `C`    | 8    | Par BC — dados ou endereçamento         |
| `D`, `E`    | 8    | Par DE — dados ou endereçamento         |
| `H`, `L`    | 8    | Par HL — ponteiro indireto de memória   |
| `SP`        | 16   | Stack Pointer                           |
| `PC`        | 16   | Program Counter                         |

### Flags de Status

| Flag  | Bit | Condição                                          |
|-------|-----|---------------------------------------------------|
| `sf`  | 7   | Sign — resultado negativo (bit 7 do resultado)    |
| `zf`  | 6   | Zero — resultado é zero                           |
| `acf` | 4   | Auxiliary Carry — carry do nibble inferior        |
| `pf`  | 2   | Parity — número par de bits 1 no resultado        |
| `cf`  | 0   | Carry — overflow de 8 bits                        |

### Mapa de Memória

```
0x0000 ┌─────────────────────────────────────────────────┐
       │  ROM (Arcade) ou Zero Page (CP/M)               │
       │  Arcade: ROM somente-leitura protegida via bitmask│
       │  CP/M:   JMP 0xF800 em 0x0005 (vetor BDOS)      │
0x0100 ├─────────────────────────────────────────────────┤
       │                                                 │
       │              TPA — Transient Program Area       │
       │         (~62 KB — programas .COM e VRAM)        │
       │                                                 │
       │   Arcade: VRAM bitmap a partir de 0x2400        │
       │   CP/M:   Programas carregados em 0x0100        │
       │                                                 │
0xF740 ├─────────────────────────────────────────────────┤
       │  DIRBUF (128 bytes) — buffer de diretório       │
0xF7C0 ├─────────────────────────────────────────────────┤
       │  DPB (15 bytes)  — Disk Parameter Block         │
0xF7D0 ├─────────────────────────────────────────────────┤
       │  ALV (dummy)     — Allocation Vector            │
0xF7E0 ├─────────────────────────────────────────────────┤
       │  DPH (16 bytes)  — Disk Parameter Header        │
0xF800 ├─────────────────────────────────────────────────┤
       │  BDOS entry point (interceptado pelo emulador)  │
0xF803 ├─────────────────────────────────────────────────┤
       │  BIOS stub table (16 entradas × 3 bytes)        │
0xFFFF └─────────────────────────────────────────────────┘
```

### Proteção de ROM

O campo `memWritable` é um bitmap de 1 bit por endereço (8 KB total). Quando `memWritable[addr/8] & (1 << (addr%8))` é 0, a escrita é silenciosamente ignorada — simulando uma EPROM real.

```cpp
// Protege 0x0000–0x1FFF como ROM somente-leitura (Space Invaders)
cpu->SetRomRegion(0x0000, 0x2000);

// Libera 0x8000–0xBFFF como RAM gravável
cpu->SetRamRegion(0x8000, 0x4000);
```

### Conjunto de Instruções Completo

| Categoria              | Instruções                                                              |
|------------------------|-------------------------------------------------------------------------|
| Transferência de dados | `MOV`, `MVI`, `LXI`, `LDA`, `STA`, `LHLD`, `SHLD`, `LDAX`, `STAX`, `XCHG` |
| Aritmética             | `ADD`, `ADC`, `SUB`, `SBB`, `ADI`, `ACI`, `SUI`, `SBI`, `DAD`, `DAA`  |
| Incremento/Decremento  | `INR`, `DCR`, `INX`, `DCX`                                              |
| Lógica                 | `ANA`, `ORA`, `XRA`, `CMP`, `ANI`, `ORI`, `XRI`, `CPI`                 |
| Rotação                | `RLC`, `RRC`, `RAL`, `RAR`                                              |
| Saltos                 | `JMP`, `JNZ`, `JZ`, `JNC`, `JC`, `JPE`, `JPO`, `JP`, `JM`              |
| Chamadas / Retornos    | `CALL`, `RET` e variantes condicionais, `RST 0–7`, `PCHL`              |
| Stack                  | `PUSH`, `POP` (B, D, H, PSW), `XTHL`, `SPHL`                           |
| I/O                    | `IN D8`, `OUT D8`                                                       |
| Controle               | `NOP`, `HLT`, `EI`, `DI`, `CMA`, `CMC`, `STC`                          |

### Tabela de Ciclos (exemplos)

```cpp
static const uint8_t OPCODE_CYCLES[256] = {
    // x0   x1   x2   x3   x4   x5   x6   x7  ...
       4,   10,   7,   5,   5,   5,   7,   4,  // 0x: NOP LXI STAX INX INR DCR MVI RLC
       5,    5,   5,   5,   5,   5,   7,   5,  // 4x: MOV r,r (5c) | MOV r,M (7c)
       4,    4,   4,   4,   4,   4,   7,   4,  // 8x: ADD/ADC (4c) | ADD M (7c)
      17,   10,  10,  10,  11,  11,   7,  11,  // Cx: CALL(17) POP(10) JNZ(10) PUSH(11)
};
```

| Instrução | Ciclos | Instrução | Ciclos |
|-----------|--------|-----------|--------|
| NOP       | 4      | LXI rp    | 10     |
| MOV r, r  | 5      | CALL      | 17     |
| MOV r, M  | 7      | RET       | 10     |
| MVI r, D8 | 7      | RST n     | 11     |
| ADD r     | 4      | XTHL      | 18     |
| ADD M     | 7      | PUSH/POP  | 11/10  |

### Exemplo de código Assembly 8080

O arquivo `TEST.ASM` na raiz do projeto demonstra o funcionamento básico:

```asm
; Exemplo: somar dois números de 8 bits e guardar no resultado
ORG 0100H

START:
    MVI A, 42H      ; A = 0x42
    MVI B, 10H      ; B = 0x10
    ADD B           ; A = A + B = 0x52
    STA 0200H       ; salva resultado em 0x0200
    HLT             ; para a CPU

ORG 0200H
RESULT: DB 0        ; espaço para o resultado

END START
```

```asm
; Exemplo: loop de contagem regressiva
ORG 0100H

COUNTDOWN:
    MVI C, 0AH      ; C = 10 (contador)
LOOP:
    DCR C           ; C = C - 1
    JNZ LOOP        ; se C != 0, continua
    HLT             ; C == 0, termina

END COUNTDOWN
```

```asm
; Exemplo: copiar bloco de memória (HL → DE, B bytes)
ORG 0100H

MEMCOPY:
    LXI H, SRC      ; HL = endereço fonte
    LXI D, DST      ; DE = endereço destino
    MVI B, 08H      ; B = 8 bytes para copiar
COPY_LOOP:
    MOV A, M        ; A = [HL]
    STAX D          ; [DE] = A
    INX H           ; HL++
    INX D           ; DE++
    DCR B           ; B--
    JNZ COPY_LOOP   ; continua se B != 0
    RET

SRC: DB 01H,02H,03H,04H,05H,06H,07H,08H
DST: DS 8

END MEMCOPY
```

---

## Núcleo da CPU — Zilog Z80

O Z80 é implementado como uma subclasse de `intel8080`, herdando o espaço de memória de 64 KB e a infraestrutura de timing. Registradores extras do Z80:

| Registrador         | Bits | Descrição                                    |
|---------------------|------|----------------------------------------------|
| `IX`, `IY`          | 16   | Registradores de índice com deslocamento      |
| `I`                 | 8    | Vetor de interrupção (modo IM2)               |
| `R`                 | 8    | Refresh de memória DRAM                       |
| `IM`                | 2    | Modo de interrupção (0, 1 ou 2)               |
| `IFF1`, `IFF2`      | 1    | Flip-flops de interrupção (EI/DI/NMI)         |
| `A'B'C'D'E'H'L'F'` | 8    | Registradores shadow (EX AF,AF' / EXX)        |
| Flags `Y`, `X`, `N` | 1    | Flags não-documentadas do F                   |

### Modos de Interrupção Z80

```
IM0 — Coloca opcode no data bus (compatível 8080, executa RST n)
IM1 — Sempre salta para 0x0038
IM2 — Tabela de vetores: endereço = (I << 8) | data_bus_byte
```

O emulador suporta todos os três modos. No modo MSX, o VDP usa IM1 (VBlank → 0x0038).

### Prefixos de opcode Z80

| Prefixo | Extensão de instrução              |
|---------|------------------------------------|
| `CB`    | Bit manipulation (BIT, SET, RES, rotações avançadas) |
| `DD`    | Instruções IX (equivalentes de HL com IX+d)           |
| `FD`    | Instruções IY (equivalentes de HL com IY+d)           |
| `ED`    | Instruções estendidas (LDIR, LDDR, CPIR, IN r,(C), etc.) |

### Usar o Z80 em jogos Arcade

```bash
# Forçar Z80 via linha de comando (sobrepõe game.cfg)
./Emulator invaders z80

# Ou definir no game.cfg:
# cpu=z80
```

---

## Modo ARCADE

### Timing e Interrupts

O timing deriva inteiramente do `game.cfg`, tornando qualquer hardware arcade configurável:

```
Clock:       config.cpuHz          (padrão: 2.000.000 Hz)
Frame rate:  config.interruptHz    (padrão: 60 FPS)
Scanlines:   config.arcadeScanlines (padrão: 256)

Ciclos por scanline = (1/fps / scanlines) × cpuHz
                    = (1/60  / 256)      × 2.000.000 ≈ 130 ciclos

Interrupt 1 (rst_mid = 0x08): metade da tela (scanline 128)
Interrupt 2 (rst_end = 0x10): fim da tela   (scanline 256)
```

### Loop Principal (simplificado)

```cpp
// A cada frame (1/60s):
while (cpu->cycles < totalCycles) {
    // 1. Verificar breakpoint
    if (breakpointActive && cpu->PC == breakpointAddr) pause();

    // 2. Interrupt de meia tela
    if (cpu->cyclesInterrupt >= midScanline && !midTriggered && cpu->interrupts) {
        ISR(cpu, 0x08);   // RST 1 → executa código em 0x0008
        midTriggered = true;
    }
    // 3. Interrupt de fim de tela
    if (cpu->cyclesInterrupt >= fullScanline && cpu->interrupts) {
        ISR(cpu, 0x10);   // RST 2 → executa código em 0x0010
        midTriggered = false;
    }

    // 4. Executar opcode
    ExecuteOpCode(cpu->memory[cpu->PC], cpu);
}

// 5. Renderizar VRAM → textura OpenGL
DrawScreen(cpu, shader, VAO, texture, vramStart, vramEnd, screenW, screenH);
```

### VRAM e Espelhamento (Space Invaders)

```
0x2000–0x3FFF: VRAM principal (lida pela GPU para renderização)
0x4000–0x5FFF: espelho da VRAM (escrita espelhada automaticamente)
0x0000–0x1FFF: ROM protegida (escritas ignoradas silenciosamente)

WriteMem com arcadeMode=true:
    addr 0x2000–0x3FFF  →  memory[addr] = val AND memory[addr+0x2000] = val
    addr 0x4000–0x5FFF  →  memory[addr] = val AND memory[addr-0x2000] = val
```

### Hardware Específico — Shift Register (Space Invaders)

O hardware original usa um shift register de 16 bits externo (chip MB14241) para rotação de sprites. O emulador o implementa via portas I/O:

```cpp
// Port 2 (write): configura o offset (bits 0-2)
//   cpu->IOPorts[2] = 3  → shift de 3 bits
//
// Port 4 (write): empurra byte no shift register de 16 bits
//   shiftRegister = (shiftRegister >> 8) | (valor << 8)
//
// Port 3 (read): lê o resultado do shift
//   retorno = (shiftRegister >> (8 - shiftOffset)) & 0xFF

// Código original do Space Invaders para ler posição de sprite:
//   MVI A, 3        ; offset de 3 bits
//   OUT 2           ; configura shift
//   MOV A, spriteX  ; posição X
//   OUT 4           ; carrega no shift register
//   IN  3           ; lê resultado shiftado
```

### Controles

**Space Invaders:**

| Tecla         | Ação              | Porta I/O |
|---------------|-------------------|-----------|
| `5`           | Inserir ficha     | Port 1 bit 0 |
| `1`           | P1 Start          | Port 1 bit 2 |
| `←` / `→`    | Mover nave        | Port 1 bit 5 / bit 6 |
| `Space`       | Atirar            | Port 1 bit 4 |

### Adicionar um Novo Jogo Arcade

1. Criar o diretório `roms/meujogo/`
2. Copiar os arquivos ROM para ele
3. Criar o `game.cfg`:

```ini
title=Meu Jogo Arcade
mode=arcade
cpu=8080
cpu_hz=2000000
interrupt_hz=60
scanlines=256
rst_mid=0x08
rst_end=0x10
vramStart=0x2400
vramEnd=0x4000
screenW=224
screenH=256
# ROMs carregadas sequencialmente a partir de 0x0000
rom_part1.bin
rom_part2.bin
# Ou com endereço explícito:
# rom_part1.bin@0x0000
# rom_part2.bin@0x0800
```

4. Executar:
```bash
./Emulator meujogo
```

---

## Modo CP/M 2.2

### Inicialização

```
CPMInit() / CCPInit() configura:
  1. 0x0000: JMP 0xF800  (vetor warm-boot)
  2. 0x0005: JMP 0xF800  (vetor BDOS — interceptado antes de executar)
  3. 0xF7C0: DPB (Disk Parameter Block)
  4. 0xF7E0: DPH (Disk Parameter Header)
  5. 0xF800: RET           (BDOS stub — interceptado)
  6. 0xF803–0xF842: BIOS stub table (16 funções × 3 bytes)
  7. Mapeia A:–P: para subdiretórios do host
  8. PC = 0x0100 (TPA — início dos programas .COM)
```

### Sessão CP/M — Exemplos de uso

Após `./Emulator cpm`, aparece o prompt `A>`:

```
CP/M 2.2 Emulator
A>
```

**Listar arquivos:**
```
A>DIR
A>DIR *.COM
A>DIR ED.*
A>LS          ; alias de DIR
```

**Trocar de drive:**
```
A>B:
B>A:
A>
```

**Executar um programa .COM:**
```
A>ZORK1
ZORK I: The Great Underground Empire

West of House
You are standing in an open field west of a white house...
```

**Copiar arquivo (PIP):**
```
A>PIP B:=A:MYFILE.TXT
A>PIP B:DEST.TXT=A:SRC.TXT[V]
```

**Editar arquivo (ED):**
```
A>ED MYFILE.TXT
*I
Linha 1 do arquivo
Linha 2 do arquivo
^Z
*E
```

**Deletar arquivo:**
```
A>ERA TEMP.TMP
A>DEL TEMP.TMP
A>ERA *.*           ; pede confirmação
Delete all files (Y/N)?
```

**Renomear:**
```
A>REN NOVO.TXT=VELHO.TXT
A>MV NOVO.TXT=VELHO.TXT
```

**Exibir conteúdo:**
```
A>TYPE README.TXT
```

**Mudar user (0–15):**
```
A>USER 3
A3>USER 0
A>
```

**Variáveis de ambiente:**
```
A>SET PATH=A: B: C:
A>SET MYVAR=hello
A>TYPE $MYVAR       ; expansão via $VAR ou %VAR%
```

**Pipe e redirecionamento:**
```
A>DIR | TYPE        ; pipe entre comandos
A>DIR > FILES.TXT   ; redireciona saída para arquivo
A>TYPE < INPUT.TXT  ; redireciona entrada de arquivo
```

### Funções BDOS Implementadas

| Fn (hex) | Nome              | Registradores de entrada / saída                             |
|----------|-------------------|--------------------------------------------------------------|
| `0x00`   | System Reset      | —  → warm-boot                                               |
| `0x01`   | Console Input     | — → A = caractere lido (bloqueia se fila vazia)              |
| `0x02`   | Console Output    | E = char → imprime no terminal virtual                       |
| `0x03`   | Reader Input      | — → A = byte da serial/reader                               |
| `0x04`   | Punch Output      | E = byte → envia para serial/punch                          |
| `0x05`   | Printer Output    | E = char → envia para impressora                             |
| `0x06`   | Direct I/O        | E = char (0xFF=read) → A = char ou status                    |
| `0x09`   | Print String      | DE = endereço → imprime até `$`                              |
| `0x0A`   | Read Buffer       | DE = FCB → lê linha do terminal (bloqueia)                   |
| `0x0B`   | Console Status    | — → A = 0xFF se há char, 0x00 se não                         |
| `0x0C`   | Return Version    | — → HL = 0x0022 (CP/M 2.2)                                  |
| `0x0D`   | Reset Disk        | — → reset drive e DMA                                        |
| `0x0E`   | Select Drive      | E = drive (0=A, 1=B, …) → seleciona drive atual             |
| `0x0F`   | Open File         | DE = FCB → A = 0x00 se ok, 0xFF se erro                     |
| `0x10`   | Close File        | DE = FCB → A = 0x00                                          |
| `0x11`   | Search First      | DE = FCB (wildcards) → A = 0xFF se não encontrou            |
| `0x12`   | Search Next       | — → A = 0xFF se não há mais                                  |
| `0x13`   | Delete File       | DE = FCB → remove arquivo do host                            |
| `0x14`   | Read Sequential   | DE = FCB → lê 128 bytes no DMA                               |
| `0x15`   | Write Sequential  | DE = FCB → escreve 128 bytes do DMA                          |
| `0x16`   | Make File         | DE = FCB → cria arquivo no host                              |
| `0x17`   | Rename File       | DE = FCB (nome antigo + novo) → renomeia                     |
| `0x19`   | Return Drive      | — → A = drive atual (0=A, 1=B, …)                            |
| `0x1A`   | Set DMA Address   | DE = endereço → define buffer DMA                            |
| `0x1F`   | Get Disk Params   | — → HL = endereço do DPH                                     |
| `0x21`   | Read Random       | DE = FCB → lê 128 bytes pelo campo random                    |
| `0x22`   | Write Random      | DE = FCB → escreve 128 bytes pelo campo random               |
| `0x24`   | Return File Size  | DE = FCB → HL = tamanho em registros de 128 bytes            |
| `0x60`   | LoadOverlay †     | C=96, DE=endereço destino, HL=FCB → A=0/0xFF, HL=bytes       |
| `0x61`   | QueryOverlay †    | — → HL=overlayBase, DE=overlayTop                            |

† Extensões customizadas para suporte a overlays (programas maiores que a TPA).

### FCB — File Control Block

Estrutura de 36 bytes que o BDOS usa para acessar arquivos:

```
Offset  Tam  Campo            Descrição
  0      1   Drive            0=drive atual, 1=A:, 2=B:, …
  1–8    8   Nome             8 chars, espaços à direita
  9–11   3   Extensão         3 chars, espaços à direita
  12     1   Extent low       número de extent atual
  14     1   Extent high
  15     1   Record count     registros de 128 bytes no extent atual
  16–31  16  (interno BDOS)   ponteiros de blocos de dados
  32     1   Current Record   posição de acesso sequencial (0–127)
  33–35  3   Random Record    posição de acesso aleatório (24 bits)
```

**Exemplo em Assembly — abrir e ler arquivo:**
```asm
; Abrir HELLO.TXT em A:
FCB:    DB 01H              ; drive A:
        DB 'HELLO   '       ; nome (8 chars, espaços)
        DB 'TXT'            ; extensão (3 chars)
        DS 25               ; restante do FCB (zeros)

        LXI D, FCB
        MVI C, 0FH          ; fn 15 = Open File
        CALL 0005H          ; chama BDOS
        CPI 0FFH            ; A == 0xFF → erro
        JZ  ERROR

        LXI D, FCB
        MVI C, 14H          ; fn 20 = Read Sequential
        CALL 0005H          ; lê 128 bytes para [DMAAddress]
```

### Terminal Virtual — Tipos Suportados

| Tipo         | game.cfg           | Descrição                               |
|--------------|--------------------|-----------------------------------------|
| ADM-3A       | `terminal=adm3a`   | Lear Siegler ADM-3A + ANSI/VT100 (padrão) |
| IBM 3101     | `terminal=ibm3101` | IBM 3101 ASCII Display Terminal          |
| Visual 200   | `terminal=visual200`| Visual Technology Visual 200 (VT100)   |

**Sequências de escape suportadas (ADM-3A/VT100):**

| Sequência           | Função                        |
|---------------------|-------------------------------|
| `ESC = row col`     | Cursor positioning (ADM-3A)   |
| `ESC [ n ; m H`     | Cursor position (VT100 CUP)   |
| `ESC [ 2 J`         | Clear screen                  |
| `ESC [ K`           | Erase to end of line          |
| `ESC [ n A/B/C/D`   | Cursor movement               |
| `ESC 7` / `ESC 8`   | Save / restore cursor (DECSC/DECRC) |
| `ESC [ ? 25 h/l`    | Show/hide cursor              |
| `\r`, `\n`, `\b`    | CR, LF, Backspace             |

### Disco CP/M — Imagens DSK

Além de mapear diretórios do host como drives, o emulador suporta imagens de disco raw:

```bash
# Montar uma imagem DSK via CCP (dentro do emulador):
MOUNT A: minha_imagem.dsk

# A geometria é detectada automaticamente:
# - IBM 8" SD (77×26×128 bytes = 243 KB)
# - IBM 8" DD (77×26×256 bytes = 486 KB)
# - Arquivo .geo sidecar (campo spt, bsh, dsm, drm, off, skew)
```

**Formato do arquivo .geo (geometria de disco):**
```ini
# minha_imagem.dsk.geo
spt=26      # setores lógicos por trilha
bsh=3       # block shift (block = 2^bsh × 128 bytes)
dsm=242     # maior número de bloco
drm=63      # maior entrada de diretório (drm+1 entradas)
off=2       # trilhas reservadas (boot)
```

### Serial Simulada (CP/M)

O emulador implementa uma porta serial que pode ser acessada via:

1. **TCP** — conectar com `nc localhost <port>` ou `telnet localhost <port>`
2. **Named pipe** — usar um FIFO do sistema operacional
3. **Console** — stdin/stdout direto (modo Altair)

```ini
# game.cfg: modo TCP na porta 5000
serial_port=5000
serial_baud=9600

# game.cfg: modo FIFO
serial_fifo_path=/tmp/cpm-serial
serial_baud=19200

# game.cfg: modo console (Altair)
serial_console=yes
```

```bash
# Conectar ao emulador via TCP (em outro terminal):
nc localhost 5000

# Criar FIFO e conectar:
mkfifo /tmp/cpm-serial
cat /tmp/cpm-serial &     # lado de leitura
echo "hello" > /tmp/cpm-serial  # envia para o emulador
```

### NVRAM — Salvar e Restaurar Estado

O emulador salva automaticamente o estado completo (CPU + memória + discos + terminal) ao fechar, e restaura na próxima execução:

```
roms/<jogo>/emulator.nvram   ← arquivo de estado automático
```

No painel de debug ImGui (modo CP/M):
- **Save State** — salva manualmente
- **Load State** — restaura manualmente
- **Auto-save** — checkbox para salvar ao fechar

---

## Modo Altair 8800

O Altair 8800 (1975) foi o primeiro computador pessoal a usar o Intel 8080. Neste modo:

- **Sem janela gráfica** — stdin/stdout se tornam o terminal serial
- O MITS BASIC 4.0 é carregado como EPROM em 0x0000
- A USART 8251 é simulada nas portas I/O 0x00 (status) e 0x01 (dados)

```ini
# roms/altair/game.cfg
title=Altair 8800 — MITS BASIC 4.0
mode=altair
eprom=0x0000,4kbas40.bin
serial_console=yes
```

```bash
./Emulator altair
```

**Sessão de exemplo com MITS BASIC:**
```
MEMORY SIZE? 64000
TERMINAL WIDTH? 72
WANT SIN-COS-TAN-ATN? Y

Ok
PRINT 2+2
 4
Ok
FOR I=1 TO 5: PRINT I: NEXT I
 1
 2
 3
 4
 5
Ok
10 PRINT "Hello, Altair!"
20 GOTO 10
RUN
Hello, Altair!
Hello, Altair!
^C
Ok
```

### Porta 88-SIO (Altair)

| Porta | Direção | Função                              |
|-------|---------|-------------------------------------|
| 0x00  | Leitura | Status: bit 0 = TX ready, bit 1 = RX available |
| 0x01  | Leitura | Dado RX (byte recebido)             |
| 0x00  | Escrita | Controle da USART                   |
| 0x01  | Escrita | Dado TX (byte a enviar)             |

---

## Modo MSX

O MSX é um padrão de computador doméstico japonês de 1983. O emulador implementa:

### TMS9918A — Video Display Processor

```
VRAM:     16 KB separados (não compartilham o espaço 64 KB da CPU)
Porta 98h: leitura/escrita de dados VRAM
Porta 99h: controle (registradores R0–R7) e leitura de status
Saída:    256×192 pixels, 15 cores + transparente
```

**Modos de vídeo:**

| Modo  | Descrição                  | Tiles  | Sprites |
|-------|----------------------------|--------|---------|
| G1    | Text 32×24 caracteres      | 256×8  | 32      |
| G2    | Graphics 256×192           | 256×8  | 32      |
| MC    | Multicolor 64×48 blocos    | —      | 32      |
| T1    | Text 40×24 monocromático   | 256×8  | —       |

### AY-3-8910 — PSG (Programmable Sound Generator)

O PSG possui 16 registradores, controlados via:
- **Porta 0xA0** — latch de endereço
- **Porta 0xA1** — escrita de dado
- **Porta 0xA2** — leitura de dado

Atualmente o estado dos registradores é capturado mas sem saída de áudio (stub).

### 8255 PPI — Parallel Peripheral Interface

| Porta | Função                                                    |
|-------|-----------------------------------------------------------|
| 0xA8  | Slot select — bits [1:0]=page0, [3:2]=page1, [5:4]=page2, [7:6]=page3 |
| 0xA9  | Teclado — dado da matriz (active low, leitura)            |
| 0xAA  | Teclado — seleção de linha [bits 3:0]                     |

### Mapa de Slots MSX

```
Slot 0 — BIOS ROM (C-BIOS: 32 KB — 0x0000–0x7FFF)
Slot 1 — Cartucho A (não implementado)
Slot 2 — Cartucho B (não implementado)
Slot 3 — RAM (64 KB — mapeada pelo PPI)
```

### Configurar um Sistema MSX

```ini
# roms/msx/game.cfg
title=MSX Computer
mode=msx
interrupt_hz=60          # NTSC (use 50 para PAL)
cpu_hz=3579545           # Z80 a 3,58 MHz (NTSC)
eprom=0x0000,cbios_main_msx1.rom
```

```bash
# Obter o C-BIOS (gratuito e open-source):
wget https://cbios.sourceforge.net/cbios0.29a.zip
unzip cbios0.29a.zip
cp cbios_main_msx1.rom roms/msx/

./Emulator msx
```

---

## Interface Gráfica e Debug

### Modo ARCADE — Painel ImGui

```
┌─────────────────────────┬────────────────────────────────┐
│                         │  Registradores                  │
│   TELA DO JOGO          │  A=00 B=00 C=00 D=00 E=00       │
│   (VRAM renderizada     │  H=00 L=00 SP=FFFF PC=0100      │
│    via OpenGL)          │  Flags: S=0 Z=0 AC=0 P=0 C=0    │
│                         │─────────────────────────────────│
│                         │  Breakpoint: [____]  [Set]       │
│                         │  [Run] [Pause] [Step] [Frame]   │
│                         │  Speed: [────────────────]       │
│                         │─────────────────────────────────│
│                         │  Histórico de instruções:        │
│                         │  0100: MOV A,B                   │
│                         │  0101: ADD C                     │
│                         │  0102: JNZ 0120                  │
│                         │  ...                             │
│                         │─────────────────────────────────│
│                         │  Memory Editor                   │
│                         │  0000: 00 01 02 03 04 05 ...     │
└─────────────────────────┴────────────────────────────────┘
```

**Controles de debug:**
- **Run / Pause** — inicia ou pausa a execução
- **Step** — executa exatamente uma instrução
- **Run Frame** — executa um frame completo (256 scanlines)
- **Breakpoint** — digitar endereço hex e pressionar Enter
- **Speed slider** — ajusta `oneInstructionCycle` (ciclos entre renders)

### Modo CP/M — Painel ImGui

```
┌──────────────────────────┬──────────────────────────────────┐
│                          │  CPU: A=00 BC=0000 DE=0000        │
│   TERMINAL ADM-3A        │      HL=0000 SP=F800 PC=0105      │
│   (80×24 colunas)        │  Flags: S=0 Z=1 AC=0 P=0 C=0     │
│                          │────────────────────────────────── │
│   A>DIR                  │  BDOS fn: 0x09 (Print String)     │
│   ED.COM    PIP.COM      │  Stack: [F800] [0105] [0200]      │
│   ASM.COM   LOAD.COM     │────────────────────────────────── │
│                          │  Target MHz: [2.0] (0=ilimitado)  │
│   A>_                    │────────────────────────────────── │
│                          │  Histórico:                        │
│                          │  F800: RET                         │
│                          │  0105: CALL F800                   │
│                          │  0102: MVI C,09                    │
│                          │────────────────────────────────── │
│                          │  [Run] [Pause] [Step]              │
│                          │  [Save State] [Load State]         │
│                          │  Breakpoint: [____]                │
│                          │────────────────────────────────── │
│                          │  Memory Editor (addr: [F800])      │
│                          │  F800: C9 00 00 00 ...             │
└──────────────────────────┴──────────────────────────────────┘
```

**Recursos exclusivos do modo CP/M:**
- **Target MHz** — limita a velocidade da CPU (0 = sem limite = máximo)
- **Stuck detection** — pausa automaticamente se a CPU entrar em loop infinito
- **BDOS monitor** — exibe nome e número da última chamada BDOS
- **Stack viewer** — mostra os últimos valores empilhados
- **NVRAM auto-save** — salva estado ao fechar a janela

### Throttle de CPU (CP/M / Altair)

O controle de velocidade usa `clock_gettime(CLOCK_MONOTONIC)` em unidades de 100 ns:

```cpp
// A cada opcode:
throttleCycles += OPCODE_CYCLES[op];
unsigned long long now = GetCurrentTime100ns();   // unidades de 100 ns
double elapsed = (now - throttleEpoch) * 1e-7;   // converte para segundos
uint64_t allowed = (uint64_t)(targetMHz * 1e6 * elapsed);

if (throttleCycles > allowed) {
    double ahead = (throttleCycles - allowed) / (targetMHz * 1e6);
    unsigned int us = (unsigned int)(ahead * 1e6);   // converte para µs
    if (us > 1000) us = 1000;   // dorme no máximo 1 ms por vez
    usleep(us);
}
// Reset do epoch a cada 10 segundos (evita overflow de uint64_t)
if (now - throttleEpoch > 100000000ULL) {
    throttleEpoch = now;
    throttleCycles = 0;
}
```

---

## Suite de Testes

O projeto inclui um runner headless (sem GUI) para validar a implementação da CPU contra ROMs de diagnóstico padrão da indústria.

### Compilar e Executar

```bash
# Compilar o runner headless
make diag_runner

# Testar o 8080 (sanity + exerciser completo)
make test-8080

# Testar o Z80 (instruções documentadas)
make test-z80

# Testar o Z80 (todas as instruções, incluindo não-documentadas)
make test-z80-all
```

### ROMs de Diagnóstico

| ROM            | CPU  | Descrição                                              |
|----------------|------|--------------------------------------------------------|
| `8080PRE.COM`  | 8080 | Sanity check — 50 ciclos, verificação rápida           |
| `8080EXM.COM`  | 8080 | Exerciser completo — 25.000 ciclos, verifica todas as instruções e flags |
| `zexdoc.com`   | Z80  | Instruções documentadas Z80 (~46 bilhões de ciclos de opcode) |
| `zexall.com`   | Z80  | Todas as instruções Z80, incluindo não-documentadas    |

### Saída Esperada (8080EXM)

```
8080 CPU Exerciser
dad <b,d,h,sp>................OK
aluop nn......................OK
aluop <b,c,d,e,h,l,m,a>.......OK
...
Tests complete
```

Qualquer linha com `ERROR` ou `FAILED` indica um bug na implementação.

---

## Dependências Externas

| Biblioteca       | Versão     | Uso                                          | Incluída |
|------------------|------------|----------------------------------------------|----------|
| **GLFW 3**       | ≥ 3.3      | Janela, contexto OpenGL, input de teclado    | Não (APT)|
| **GLAD**         | OpenGL 3.3 | Loader de extensões OpenGL                   | Sim      |
| **Dear ImGui**   | latest     | Interface de debug                            | Sim      |
| **GLM**          | latest     | Matemática vetorial/matricial (shaders)      | Sim      |
| **KHR**          | —          | Headers de plataforma OpenGL                 | Sim      |

Apenas o **GLFW** precisa ser instalado via APT. Todas as demais dependências estão em `lib/` e `include/`.

---

## Melhorias Possíveis

### CPU e Emulação

- **Ciclos condicionais** — `CALL` e `RET` condicionais têm custo diferente quando a condição é verdadeira vs. falsa. A tabela atual usa um valor único, causando imprecisão de timing.
- **Instrução DAA** — `DAA` (Decimal Adjust Accumulator) para BCD pode não estar totalmente correta. Testar com 8080EX1.
- **Opcodes não documentados** — o 8080 tem alguns opcodes não documentados que alguns programas usam.
- **NMI no Z80** — a interrupção não mascarável (NMI → 0x0066) ainda não está implementada.

### Modo ARCADE

- **Suporte a som** — o Space Invaders usa chips de som dedicados (portas 3 e 5). SDL_mixer ou PortAudio adicionaria os efeitos sonoros originais.
- **Mais jogos** — Balloon Bomber, Lunar Rescue e outros da Midway usam hardware similar ao Space Invaders.
- **Save state** — serializar CPU + VRAM + IOPorts para arquivo.
- **Replay de input** — capturar o estado inicial e eventos I/O para replays determinísticos.

### Modo CP/M

- **Mais de 16 drives** — o sistema suporta A:–P: (16 drives). A especificação CP/M 2.2 limita a 16, mas drives adicionais são comuns em implementações estendidas.
- **CP/M 3.0 (Plus)** — implementar as chamadas extras do CP/M 3.0 aumentaria a compatibilidade.
- **Suporte a MP/M** — sistema multi-usuário baseado em CP/M.

### MSX

- **Saída de áudio** — o AY-3-8910 captura os registradores mas não gera áudio. SDL_audio ou PortAudio completaria a implementação.
- **Sprites com colisão** — o status de colisão e 5° sprite do TMS9918A está parcialmente implementado.
- **Cartuchos (slots 1 e 2)** — suporte a ROMs de cartucho MSX (.rom) carregadas em slot 1 ou 2.
- **MSX-DOS** — implementar o MSX-DOS (derivado do CP/M 2.2) nos slots de RAM.

### Interface Gráfica

- **Disassembler prospectivo** — mostrar as próximas N instruções a partir do PC atual.
- **Watchpoints de memória** — pausar quando um endereço é lido ou escrito.
- **Gráfico de MHz em tempo real** — monitorar o throttle visualmente.
- **HiDPI** — detectar DPI do monitor e escalar a interface.

### Build e Infraestrutura

- **CMake** — migrar de Makefile para CMake facilitaria builds em Windows e macOS.
- **Target `make release`** — adicionar `-O2 -DNDEBUG` para builds de produção.
- **Testes unitários** — Google Test ou Catch2 para testar ALU de forma isolada.
- **Suporte a Windows** — substituir `usleep()` por `Sleep()` do Win32.
- **Sanitizers** — `-fsanitize=address,undefined` para detectar bugs de memória.

---

## Referências

- [Intel 8080 Programmer's Manual (1975)](https://archive.org/details/8080asm)
- [Zilog Z80 CPU User Manual](http://www.zilog.com/appnotes_download.php?FromPage=DirectLink&dn=UM0080&ft=User%20Manual&f=YUhR0QwiQSkunl2)
- [CP/M 2.2 Programmer's Manual — Digital Research](http://www.cpm.z80.de/randyfiles/DRI/CPM22.pdf)
- [Space Invaders Hardware Reference — Computer Archeology](http://www.computerarcheology.com/Arcade/SpaceInvaders/Hardware.html)
- [ADM-3A Terminal Reference Manual](http://bitsavers.org/pdf/lear_siegler/ADM3A_Users_Manual.pdf)
- [TMS9918A/TMS9928A Datasheet — Texas Instruments](http://www.bitsavers.org/components/ti/TMS9900/TMS9918A_TMS9928A_TMS9929A_Video_Display_Processors_Apr82.pdf)
- [AY-3-8910 Datasheet — General Instrument](https://github.com/mamedev/mame/blob/master/src/devices/sound/ay8910.cpp)
- [MSX Technical Handbook](https://github.com/Konamiman/MSX2-Technical-Handbook)
- [C-BIOS — Free MSX1 BIOS](https://cbios.sourceforge.net/)
- [8080 / Z80 Exerciser ROMs](https://github.com/begoon/i8080-core/tree/master/asm)
