# Emulador Intel 8080

Emulador completo do processador Intel 8080 escrito em C++17 com interface gráfica via OpenGL 3.3 + ImGui. Suporta dois modos de operação distintos: **ARCADE** (jogos clássicos como Space Invaders) e **CP/M 2.2** (sistema operacional de 8 bits com shell interativo).

---

## Sumário

- [Visão Geral](#visão-geral)
- [Arquitetura do Projeto](#arquitetura-do-projeto)
- [Como Compilar](#como-compilar)
- [Como Executar](#como-executar)
- [Núcleo da CPU](#núcleo-da-cpu)
- [Modo ARCADE](#modo-arcade)
- [Modo CP/M 2.2](#modo-cpm-22)
- [Interface Gráfica e Debug](#interface-gráfica-e-debug)
- [Dependências Externas](#dependências-externas)
- [Melhorias Possíveis](#melhorias-possíveis)

---

## Visão Geral

| Característica     | Detalhes                                      |
|--------------------|-----------------------------------------------|
| CPU emulada        | Intel 8080 (8 bits, 1974)                     |
| Clock (Arcade)     | 2.0 MHz com interrupts de scanline a 60 FPS   |
| Memória            | 64 KB flat (0x0000–0xFFFF)                    |
| Gráficos           | OpenGL 3.3 Core Profile via GLFW + GLAD       |
| UI de debug        | Dear ImGui com editor hexadecimal de memória  |
| Jogos suportados   | Space Invaders, Alien Invaders, Searthie      |
| Ambiente CP/M      | CP/M 2.2 com BDOS, CCP e terminal ADM-3A      |
| Linguagem          | C++17 (g++)                                   |
| Plataforma         | Linux (testado em Ubuntu/Debian x86-64)       |

---

## Arquitetura do Projeto

```
8080Emulator_teste/
├── emulador.cpp          # Ponto de entrada, loop principal, lógica de timing
├── intel8080.h / .cpp    # Definição da CPU: registradores, memória, opcodes
├── alu.h / .cpp          # ALU: ADD, SUB, ADC, SBB, AND, OR, XOR, CMP, rotações
├── gui.h / .cpp          # Janela OpenGL, renderização da tela, frames ImGui
├── input.cpp             # Mapeamento de teclado → portas I/O (modo Arcade)
├── game_config.h / .cpp  # Parser do arquivo game.cfg por jogo/aplicação
├── hexbyte.h / .cpp      # Conversão entre string hex e inteiro
│
├── cpm_bios.h / .cpp     # Sistema CP/M: BDOS, terminal ADM-3A, disco virtual
├── cpm_ccp.h / .cpp      # Console Command Processor (shell do CP/M)
├── cpmdebugstate.h / .cpp# Estado e histórico de debug para o modo CP/M
│
├── Makefile              # Build system
├── roms/                 # Diretórios por jogo, cada um com game.cfg + ROMs
│   ├── invaders/         # Space Invaders (invaders.h/g/f/e)
│   ├── cpm/              # CP/M puro (sem ROM, inicia no CCP)
│   └── cpm_wordstar/     # WordStar 3.3 (WS.COM)
│
├── include/              # Headers de terceiros (GLFW, GLAD, GLM, ImGui, KHR)
├── lib/imgui/            # Código-fonte do Dear ImGui
└── src/glad.c            # Loader de extensões OpenGL
```

### Fluxo de Dados

```
argv[1] (nome do jogo)
       │
       ▼
  LoadGameConfig()  ─── roms/<jogo>/game.cfg ─── modo, ROMs, VRAM, título
       │
       ▼
  intel8080 (cpu)  ◄─── LoadRomFile() ─── ROM carregada em memória
       │
       ├── [ARCADE] ──► Loop 60 FPS: Execute opcodes → ISR scanline → DrawScreen()
       │
       └── [CP/M]   ──► Loop: CCP/BDOS intercept → Execute opcodes → DrawTerminal()
                                    │
                                    ▼
                              ImGui debug panel
```

---

## Como Compilar

### Requisitos

- `g++` com suporte a C++17
- `libglfw3-dev`
- `libgl-dev` (Mesa ou driver proprietário)
- `libdl` e `libpthread` (geralmente já presentes)

No Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential libglfw3-dev libgl-dev
```

### Compilação

```bash
# Clone ou entre no diretório do projeto
cd Emulator

# Compila tudo (CPU core, GUI, ImGui, GLAD, CP/M, etc.)
make

# Limpar artefatos de build
make clean
```

O `make` gera o executável `Emulator` na raiz do projeto. Os objetos intermediários ficam em `build/`.

### Flags de compilação usadas

| Flag                   | Finalidade                                           |
|------------------------|------------------------------------------------------|
| `-g`                   | Símbolos de debug (GDB)                              |
| `-I./include/`         | Headers de GLFW, GLAD, GLM, KHR                      |
| `-I./include/GL/`      | Headers OpenGL extras                                |
| `-I./include/imgui/`   | Headers do Dear ImGui                                |
| `-I./`                 | Headers locais do projeto                            |
| `-lglfw`               | Biblioteca GLFW (janela e input)                     |
| `-lGL`                 | OpenGL                                               |
| `-ldl`                 | `dlopen` (necessário para GLAD)                      |
| `-lpthread`            | Threads POSIX                                        |

---

## Como Executar

```bash
# Space Invaders (padrão)
./Emulator invaders

# Alien Invaders
./Emulator alieninv

# Searthie (clone de Space Invaders)
./Emulator searthie

# CP/M 2.2 puro (inicia no shell CCP)
./Emulator cpm

# WordStar 3.3 rodando no CP/M
./Emulator cpm_wordstar
```

Se nenhum argumento for fornecido, o padrão é `invaders`.

### Estrutura do arquivo `game.cfg`

Cada jogo tem seu próprio subdiretório em `roms/` com um arquivo `game.cfg`:

```ini
# Modo arcade (padrão se não especificado)
title=Space Invaders
invaders.h
invaders.g
invaders.f
invaders.e

# Modo CP/M — sem ROM (inicia no CCP)
mode=cpm
title=CP/M 2.2

# Modo CP/M — com .COM específico
mode=cpm
title=WordStar 3.3
WS.COM
```

---

## Núcleo da CPU

### Registradores (`intel8080.h`)

| Registrador | Bits | Descrição                              |
|-------------|------|----------------------------------------|
| `A`         | 8    | Acumulador                             |
| `B`, `C`    | 8    | Par BC (endereçamento ou dados)        |
| `D`, `E`    | 8    | Par DE (endereçamento ou dados)        |
| `H`, `L`    | 8    | Par HL (ponteiro indireto de memória)  |
| `SP`        | 16   | Stack Pointer                          |
| `PC`        | 16   | Program Counter                        |

### Flags de Status

| Flag  | Bit | Condição                                    |
|-------|-----|---------------------------------------------|
| `sf`  | 7   | Sign: resultado é negativo                  |
| `zf`  | 6   | Zero: resultado é zero                      |
| `acf` | 4   | Auxiliary Carry: carry do nibble inferior   |
| `pf`  | 2   | Parity: número par de bits 1 no resultado   |
| `cf`  | 0   | Carry: overflow de 8 bits                   |

### Mapa de Memória

```
0x0000 ┌─────────────────────────────┐
       │  ROM (Arcade) / Zero Page   │  Arcade: ROM somente-leitura
       │  ou Zero Page (CP/M)        │  CP/M:   warm-boot, FCBs, command tail
0x0100 ├─────────────────────────────┤
       │                             │
       │         TPA                 │  Transient Program Area (~62 KB)
       │  (Programas .COM, VRAM)     │  Arcade: VRAM a partir de 0x2400
       │                             │
0xF740 ├─────────────────────────────┤
       │  DIRBUF (128 bytes)         │  Buffer de diretório CP/M
0xF7C0 ├─────────────────────────────┤
       │  DPB  (15 bytes)            │  Disk Parameter Block
0xF7D0 ├─────────────────────────────┤
       │  ALV  (dummy)               │  Allocation Vector
0xF7E0 ├─────────────────────────────┤
       │  DPH  (16 bytes)            │  Disk Parameter Header
0xF800 ├─────────────────────────────┤
       │  BDOS entry point           │  Interceptado antes de executar (CP/M)
0xFFFF └─────────────────────────────┘
```

### Conjunto de Instruções Implementado

O emulador implementa todas as instruções documentadas do Intel 8080:

| Categoria              | Instruções                                                            |
|------------------------|-----------------------------------------------------------------------|
| Transferência de dados | `MOV`, `MVI`, `LXI`, `LDA`, `STA`, `LHLD`, `SHLD`, `LDAX`, `STAX`, `XCHG` |
| Aritmética             | `ADD`, `ADC`, `SUB`, `SBB`, `ADI`, `ACI`, `SUI`, `SBI`, `DAD`, `DAA`|
| Incremento/Decremento  | `INR`, `DCR`, `INX`, `DCX`                                            |
| Lógica                 | `ANA`, `ORA`, `XRA`, `CMP`, `ANI`, `ORI`, `XRI`, `CPI`               |
| Rotação                | `RLC`, `RRC`, `RAL`, `RAR`                                            |
| Saltos                 | `JMP`, `JNZ`, `JZ`, `JNC`, `JC`, `JPE`, `JPO`, `JP`, `JM`            |
| Chamadas/Retornos      | `CALL`, `RET` e variantes condicionais, `RST 0–7`, `PCHL`             |
| Stack                  | `PUSH`, `POP` (B, D, H, PSW), `XTHL`, `SPHL`                         |
| I/O                    | `IN D8`, `OUT D8`                                                     |
| Controle               | `NOP`, `HLT`, `EI`, `DI`, `CMA`, `CMC`, `STC`                        |

### Ciclos por Opcode

Cada opcode tem seu custo em ciclos definido estaticamente na tabela `OPCODE_CYCLES[256]` em `intel8080.h`. Exemplos:

| Instrução | Ciclos |
|-----------|--------|
| NOP       | 4      |
| MOV r, r  | 5      |
| MOV r, M  | 7      |
| LXI rp    | 10     |
| CALL      | 17     |
| XTHL      | 18     |

---

## Modo ARCADE

### Timing e Interrupts

O modo Arcade emula o hardware do Space Invaders com precisão de timing:

```
Clock:       2.000.000 Hz
Frame rate:  60 FPS  →  ~33.333 ciclos/frame
Scanlines:   256  →  ~130 ciclos/scanline

Interrupt 1 (RST 1, 0x08): scanline 128  — metade da tela
Interrupt 2 (RST 2, 0x10): scanline 256  — fim da tela
```

O loop principal executa opcodes até atingir o total de ciclos do frame, depois dispara os dois ISRs e renderiza a tela.

### VRAM e Espelhamento

```
Escrita em 0x2000–0x3FFF → espelhada também em 0x4000–0x5FFF
Escrita em 0x0000–0x1FFF → ignorada (ROM protegida)
```

`WriteMem()` em `intel8080.cpp` implementa esse comportamento via `arcadeMode = true`.

### Hardware Específico

O Space Invaders usa um shift register externo acessado via portas I/O. O emulador o implementa diretamente em `intel8080`:

```
Port 2 (write): define offset do shift (3 bits)
Port 4 (write): carrega valor de 8 bits no shift register de 16 bits
Port 3 (read):  lê resultado do shift: (shiftRegister >> (8 - shiftOffset)) & 0xFF
```

### Controles (Space Invaders)

| Tecla      | Ação                  |
|------------|-----------------------|
| `5`        | Inserir ficha (Coin)  |
| `1`        | P1 Start              |
| `←` / `→`  | Mover nave            |
| `Space`    | Atirar                |

---

## Modo CP/M 2.2

### Inicialização

Ao iniciar no modo CP/M, `CPMInit()` (`cpm_bios.cpp`) configura:

1. Zero page com `JMP 0xF800` em 0x0005 (vetor BDOS)
2. DPB (Disk Parameter Block) em 0xF7C0
3. DPH (Disk Parameter Header) em 0xF7E0
4. Registradores da CPU com valores iniciais do CP/M
5. Mapeamento dos drives A:/B:/C:/D: para subdiretórios do host

### Funções BDOS Implementadas

| Fn   | Nome                  | Descrição                                       |
|------|-----------------------|-------------------------------------------------|
| 0x00 | System Reset          | Warm-boot: volta ao CCP ou encerra              |
| 0x01 | Console Input         | Lê um caractere (bloqueante se fila vazia)      |
| 0x02 | Console Output        | Escreve caractere no terminal virtual           |
| 0x06 | Direct I/O            | I/O direto sem echo                             |
| 0x09 | Print String          | Imprime string terminada em `$`                 |
| 0x0A | Read Buffer           | Lê linha de input (bloqueante)                  |
| 0x0B | Console Status        | Retorna 0xFF se há caractere disponível         |
| 0x0C | Return Version        | Retorna 0x22 (CP/M 2.2)                         |
| 0x0D | Reset Disk            | Reseta seleção de drive                         |
| 0x0E | Select Drive          | Seleciona drive A–D                             |
| 0x0F | Open File             | Abre arquivo via FCB                            |
| 0x10 | Close File            | Fecha arquivo via FCB                           |
| 0x11 | Search First          | Busca arquivo com wildcard                      |
| 0x12 | Search Next           | Continua busca anterior                         |
| 0x14 | Read Sequential       | Lê 128 bytes no DMA buffer                      |
| 0x15 | Write Sequential      | Escreve 128 bytes do DMA buffer                 |
| 0x16 | Make File             | Cria novo arquivo                               |
| 0x1A | Set DMA Address       | Define endereço do buffer DMA                   |
| 0x1F | Get Disk Parameters   | Retorna endereço do DPH                         |

### FCB (File Control Block)

Estrutura de 36 bytes usada pelo BDOS para gerenciar arquivos:

```
Offset  Tamanho  Campo
0       1        Drive (0 = atual, 1 = A:, 2 = B:, ...)
1–8     8        Nome do arquivo (padded com espaços)
9–11    3        Extensão (padded com espaços)
12      1        Extent low
13      1        (reservado)
14      1        Extent high
15      1        Record count
16–31   16       (interno ao BDOS)
32      1        Current record (acesso sequencial)
33–35   3        (acesso aleatório)
```

### Console Command Processor (CCP)

O CCP (`cpm_ccp.cpp`) implementa um shell interativo com os seguintes comandos built-in:

| Comando      | Uso                          | Descrição                          |
|--------------|------------------------------|------------------------------------|
| `DIR ou LS`  | `DIR [padrão]`               | Lista arquivos (suporta wildcard `*`) |
| `ERA ou DEL` | `ERA arquivo.ext`            | Deleta arquivo                     |
| `REN ou MV`  | `REN novo.ext=velho.ext`     | Renomeia arquivo                   |
| `TYPE`       | `TYPE arquivo.ext`           | Exibe conteúdo do arquivo          |
| `USER`       | `USER n`                     | Muda user ID (0–15)                |
| `CLS`        |                              | Limpa o terminal                   |
| `VER`        |                              | Mostra versão ("CP/M 2.2 Emulator")|
| `A:/ B:/ C:/ D:`|                           | Troca de drive                     |

Qualquer outro nome é interpretado como um arquivo `.COM` a ser carregado na TPA (0x0100) e executado.

### Terminal Virtual ADM-3A

O sistema emula um terminal ADM-3A de 80×24 colunas com suporte a:

- Scroll vertical automático
- Backspace e carriage return
- Cursor positioning via escape sequence `ESC = row col`
- Clear screen

---

## Interface Gráfica e Debug

### Modo ARCADE — Painel ImGui

- Visualização em tempo real dos registradores A, B, C, D, E, H, L, SP, PC
- Flags de status (S, Z, AC, P, C)
- Histórico das últimas 1000 instruções desassembladas
- Editor hexadecimal de memória (ImGui Memory Editor)
- Breakpoint por endereço (entrada em hex)
- Controles: **Run/Pause**, **Step** (uma instrução), **Run Frame** (um frame inteiro)
- Slider de velocidade (`oneInstructionCycle`)

### Modo CP/M — Painel ImGui

- Terminal ADM-3A virtual renderizado à esquerda
- Painel de debug à direita com:
  - Registradores e flags da CPU
  - Stack atual
  - Última chamada BDOS e nome da função
  - Histórico de instruções com timestamp
  - Breakpoint por endereço
  - Throttle de CPU: campo `targetMHz` (0 = sem limite)
  - Editor de memória com endereço configurável

### Throttle de CPU (CP/M)

O emulador mede tempo real via `clock_gettime(CLOCK_MONOTONIC)` em unidades de 100 ns e calcula ciclos permitidos com base em `targetMHz`. Se estiver adiantado, dorme com `usleep()` de até 1 ms por vez. O epoch é resetado a cada 10 segundos para evitar overflow de contadores.

---

## Dependências Externas

| Biblioteca           | Versão   | Uso                                         |
|----------------------|----------|---------------------------------------------|
| **GLFW 3**           | ≥ 3.3    | Criação de janela, contexto OpenGL, input   |
| **GLAD**             | OpenGL 3.3 Core | Carregamento de ponteiros OpenGL   |
| **Dear ImGui**       | incluído | Interface gráfica de debug                  |
| **GLM**              | incluído | Matemática vetorial/matricial               |
| **KHR**              | incluído | Headers de plataforma OpenGL                |

ImGui, GLAD, GLM e KHR já estão incluídos no repositório em `lib/` e `include/`. Apenas GLFW precisa ser instalado separadamente.

---

## Melhorias Possíveis

### CPU e Emulação

- **Instrução DAA incompleta**: a instrução `DAA` (Decimal Adjust Accumulator) requer lógica precisa para BCD que pode não estar totalmente correta. Revisar e cobrir com testes usando a suite de diagnóstico 8080EX1.
- **Ciclos condicionais**: algumas instruções como `CALL` e `RET` condicionais têm custo de ciclos diferente quando a condição é verdadeira vs. falsa. A tabela atual usa um valor único, o que causa imprecisão de timing.
- **Undocumented opcodes**: o 8080 tem opcodes não documentados que alguns programas usam. Podem ser adicionados como NOPs ou com comportamento correto.
- **Suite de testes automáticos**: integrar os ROMs de diagnóstico `8080PRE.COM` e `8080EX1.COM` (Exerciser) para verificar todas as instruções e flags automaticamente.

### Modo ARCADE

- **Suporte a som**: o Space Invaders usa chips de som dedicados. Adicionar emulação de áudio via SDL_mixer ou PortAudio ativaria os efeitos sonoros originais (portas 3 e 5).
- **Outros jogos Arcade 8080**: Balloon Bomber, Lunar Rescue e outros jogos da Midway que usam hardware similar poderiam ser adicionados como novos perfis de `game.cfg` com configurações de portas I/O personalizadas.
- **Gravação e replay de input**: capturar o estado inicial e todos os eventos de I/O para criar replays determinísticos úteis para testes de regressão.
- **Save state**: serializar o estado completo da CPU + VRAM + IOPorts para arquivo, similar ao que já existe no modo CP/M.

### Modo CP/M

- **Acesso aleatório**: as funções BDOS `0x21` (Read Random) e `0x22` (Write Random) não estão implementadas. Necessárias para programas como dBASE II e WordStar em operação normal.
- **Mais de 4 drives**: o sistema atual suporta A:–D:. Expandir para A:–P: (16 drives) completaria a especificação CP/M 2.2.
- **Imagens de disco `.img`**: em vez de mapear arquivos individuais do host, suportar imagens de disco brutas (formato .img) permitiria rodar softwares com layout de disco real, incluindo o sistema de diretório nativo do CP/M.
- **CP/M 3.0 (Plus)**: a arquitetura BDOS é extensível. Implementar as chamadas adicionais do CP/M 3.0 aumentaria a compatibilidade.
- **Redirecionamento de I/O**: suporte a pipes e redirecionamento (`>`, `<`, `|`) no CCP.

### Interface Gráfica

- **Disassembler bidirecional**: o disassembler atual só mostra o histórico de instruções já executadas. Um disassembler prospectivo (mostrando as próximas N instruções a partir do PC) tornaria o debug muito mais eficiente.
- **Watchpoints de memória**: pausar a execução quando um endereço de memória específico é lido ou escrito, além dos breakpoints de PC existentes.
- **Gráfico de performance**: exibir um gráfico de MHz em tempo real no painel de debug para monitorar o throttle.
- **Tema e layout configurável**: as proporções dos painéis e temas de cores do ImGui poderiam ser persistidos em arquivo de configuração.
- **Suporte a HiDPI**: a janela atualmente usa resolução fixa. Detectar DPI do monitor e escalar a interface corretamente.

### Build e Infraestrutura

- **CMake**: migrar de Makefile para CMake facilitaria builds em Windows e macOS sem modificações manuais de flags.
- **Otimização de release**: o `Makefile` atual usa apenas `-g` (debug). Adicionar um target `make release` com `-O2 -DNDEBUG` para builds de produção.
- **Testes unitários**: usar Google Test ou Catch2 para testar instruções individuais da ALU de forma isolada e automatizada em CI.
- **Suporte a Windows**: substituir `usleep()` por `Sleep()` do Win32 e ajustar os caminhos de arquivo para usar separador `\` tornaria o projeto multiplataforma.
- **Sanitizers**: adicionar `-fsanitize=address,undefined` ao target de debug para detectar leituras fora de bounds na memória emulada.

---

## Referências

- [Intel 8080 Programmer's Manual (1975)](https://archive.org/details/8080asm)
- [CP/M 2.2 Programmer's Manual — Digital Research](http://www.cpm.z80.de/randyfiles/DRI/CPM22.pdf)
- [Space Invaders Hardware Reference — Computer Archeology](http://www.computerarcheology.com/Arcade/SpaceInvaders/Hardware.html)
- [ADM-3A Terminal Reference Manual](http://bitsavers.org/pdf/lear_siegler/ADM3A_Users_Manual.pdf)
