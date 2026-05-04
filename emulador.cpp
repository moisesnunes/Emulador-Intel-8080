#include <iostream>
#include <string>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "intel8080.h"
#include "alu.h"
#include "hexbyte.h"
#include "gui.h"
#include "game_config.h"
#include "cpm_bios.h"
#include "cpm_ccp.h"

#include <time.h>
#include <unistd.h>

unsigned long long currentTime, lastGuiUpdate = 0, clockCycle = 0, invadersUpdate = 0, interruptTimer1 = 0, interruptTimer2 = 0;

static unsigned long long GetCurrentTime100ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 10000000ULL + (unsigned long long)ts.tv_nsec / 100ULL;
}

void ISR(intel8080 *cpu, int intaddress)
{
    PushRegisterPair(cpu, ((cpu->PC & 0xff00) >> 8), (cpu->PC & 0xff));
    cpu->PC = intaddress;
    cpu->interrupts = false;
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height);
void processInput(GLFWwindow *window, intel8080 *cpu);

// settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

// interrupts
// two interrupts, both at 60hz
// one when the beam is half screen, the other is at end of screen
// when interrupt is triggered, disable interrupt
// push PC and then go to ISR

float fps = 60.0;         // frames per second
float period = 1.0 / fps; // period of a frame

float scanlines = 256.0; // scanlines per screen
float freq = 2000000;    // clock frequency

float cycleperscanline = (period / scanlines) * freq;

int main(int argc, char *argv[])
{
    std::string gameName = (argc > 1) ? argv[1] : "invaders";

    std::string exeDir = GetExeDir();
    GameConfig config = LoadGameConfig(exeDir, gameName);

    // CCP mode starts with no ROM — the CCP itself is the program.
    if (config.romFiles.empty() && config.mode != EmulatorMode::CPM)
    {
        std::cout << "No ROM files listed in roms/" << gameName << "/game.cfg" << std::endl;
        return -1;
    }

    uint16_t programCount;

    GLFWwindow *window = InitGUI(config.title);

    unsigned int shaderProgram = InitializeShader();
    unsigned int VAO = InitializeVAO();

    char PauseOnLine[5];
    int PauseOnLineInt;

    bool breakpointActive;

    const int MAXINSTRUCTIONS = 1000;
    int currentInstruction = 0;
    int previousInstructions[MAXINSTRUCTIONS * 4];

    for (int i = 0; i < MAXINSTRUCTIONS * 4; i++)
    {
        previousInstructions[i] = 0;
    }

    intel8080 *cpu = new intel8080();

    if (LoadRomFile(cpu, exeDir, config.romFiles, config.romLoadOffset) != 0)
    {
        return -1;
    }

    // ── CP/M mode ────────────────────────────────────────────────────────────
    if (config.mode == EmulatorMode::CPM)
    {
        cpu->arcadeMode = false;
        CPMState cpm;
        std::string diskDir = exeDir + "/roms/" + config.name;

        // Propagate peripheral device paths and terminal type from game.cfg.
        cpm.readerPath          = config.cpmReader;
        cpm.punchPath           = config.cpmPunch;
        cpm.printerPath         = config.cpmPrinter;
        cpm.terminal.termType   = config.cpmTerminal;

        RegisterCPMTerminalCallbacks(window, &cpm);

        if (config.romFiles.empty())
        {
            // No ROM specified → start with the CCP command prompt.
            CCPInit(cpu, cpm, diskDir);
        }
        else
        {
            // Specific .COM specified in game.cfg → load and run directly.
            CPMInit(cpu, cpm, diskDir);
            if (LoadRomFile(cpu, exeDir, config.romFiles, config.romLoadOffset) != 0)
                return -1;
        }

        CPMDebugState dbg;

        unsigned long long throttleEpoch = GetCurrentTime100ns();
        uint64_t throttleCycles = 0;

        while (!glfwWindowShouldClose(window) && cpm.running)
        {
            bool canRun = dbg.notHalted || dbg.runOnce;

            if (canRun)
            {
                if (cpm.ccpRunning)
                {
                    // CCP has control: accumulate typed characters, run commands.
                    if (CCPTick(cpu, cpm))
                        cpm.ccpRunning = false; // .COM loaded, hand off to CPU
                }
                else if (cpu->PC == 0x0000)
                {
                    // Warm-boot vector hit: program returned to 0x0000.
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
                else if (cpu->PC == 0x0005 || cpu->PC == BDOS_ADDR)
                {
                    BDOSCall(cpu, cpm);
                    // In CCP mode, fn 0 (System Reset) sets ccpRunning instead of
                    // clearing running — handled inside BDOS_SystemReset.
                }
                else if (cpu->PC >= BIOS_ADDR &&
                         cpu->PC < (uint16_t)(BIOS_ADDR + 16 * 3) &&
                         (cpu->PC - BIOS_ADDR) % 3 == 0)
                {
                    BIOSCall(cpu, cpm);
                }
                else
                {
                    // Check breakpoint before executing.
                    int bpAddr = HexToByte(dbg.pauseLine);
                    if (dbg.breakpointActive && cpu->PC == bpAddr)
                    {
                        dbg.notHalted = false;
                        dbg.breakpointActive = false;
                    }
                    else
                    {
                        uint8_t op = cpu->memory[cpu->PC];
                        dbg.logInstruction(cpu);
                        ExecuteOpCode(op, cpu);
                        throttleCycles += OPCODE_CYCLES[op];

                        if (dbg.targetMHz > 0.0) {
                            unsigned long long now = GetCurrentTime100ns();
                            double elapsed = (now - throttleEpoch) * 1e-7;
                            uint64_t allowed = (uint64_t)(dbg.targetMHz * 1e6 * elapsed);
                            if (throttleCycles > allowed) {
                                double ahead = (throttleCycles - allowed) / (dbg.targetMHz * 1e6);
                                unsigned int us = (unsigned int)(ahead * 1e6);
                                if (us > 1000) us = 1000;
                                if (us > 0) usleep(us);
                            }
                            // Reset epoch every 10 s to avoid overflow
                            if (now - throttleEpoch > 100000000ULL) {
                                throttleEpoch = now;
                                throttleCycles = 0;
                            }
                        }
                    }
                }

                if (dbg.runOnce)
                {
                    dbg.notHalted = false;
                    dbg.runOnce = false;
                }
            }

            currentTime = GetCurrentTime100ns();
            if ((currentTime - lastGuiUpdate) >= (unsigned long long)(10000000 / 60))
            {
                glfwPollEvents();
                glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                ImGUIFrameCPM(cpm, cpu, dbg);
                glfwSwapBuffers(window);
                lastGuiUpdate = currentTime;
            }
        }

        GraphicsCleanup(VAO, shaderProgram);
        return 0;
    }

    // ── Arcade mode ──────────────────────────────────────────────────────────
    bool notHalted = false;
    bool runOnce = false;
    bool runFrame = true;

    int opcode = 0;
    int firstByte = 0;
    int secondByte = 0;

    int totalCycles = 0;

    long long int oneInstructionCycle = 166666;

    unsigned long long timePerFrame = 166666;

    unsigned int texture;
    glGenTextures(1, &texture);

    bool haltatInterrupt = true;

    bool midInterruptTriggered = false;
    bool cleared = false;
    uint8_t previous = 0;
    bool runNext = false;

    uint16_t previousCoin = 0;
    uint16_t previousSwitch = 0;

    // program loop
    while (!glfwWindowShouldClose(window))
    {
        // update times
        currentTime = GetCurrentTime100ns();

        if (oneInstructionCycle <= 10000)
        {
            oneInstructionCycle = 0;
        }

        // wait for next frame or instruction
        if (runFrame)
        {
            if (((currentTime - invadersUpdate) >= timePerFrame) && notHalted)
            {
                runNext = true;
                invadersUpdate = currentTime;
                totalCycles = cycleperscanline * scanlines;
            }
        }
        else
        {
            if (((currentTime - invadersUpdate) >= oneInstructionCycle) && notHalted)
            {
                runNext = true;
                invadersUpdate = currentTime;
                totalCycles = cpu->cycles + 1;
            }
        }

        // if time for next frame, execute opcodes and ISRs
        if (runNext)
        {
            // if(((currentTime - clockCycle) >= (unsigned long long) (oneInstructionCycle)) && notHalted){
            while ((int(cpu->cycles) < totalCycles) && notHalted)
            {

                runNext = false;

                PauseOnLineInt = HexToByte(PauseOnLine);
                if ((cpu->PC == PauseOnLineInt) && breakpointActive)
                {
                    std::cout << "breakpoint" << std::endl;
                    notHalted = false;
                }

                if (breakpointActive)
                {
                    std::cout << "breakpoint" << std::endl;
                    breakpointActive = false;
                    notHalted = false;
                }

                if ((cpu->cyclesInterrupt >= cycleperscanline * (scanlines / 2)) && !midInterruptTriggered && cpu->interrupts)
                {
                    ISR(cpu, 0x08);
                    midInterruptTriggered = true;
                    cpu->cyclesInterrupt = 0;
                }
                if ((cpu->cyclesInterrupt >= cycleperscanline * (scanlines / 2)) && cpu->interrupts)
                {
                    ISR(cpu, 0x10);
                    midInterruptTriggered = false;
                    cpu->cyclesInterrupt = 0;
                }

                ExecuteOpCode(cpu->memory[cpu->PC], cpu);
                clockCycle = currentTime;

                // log instructions
                previousInstructions[(currentInstruction * 4)] = cpu->PC;
                previousInstructions[(currentInstruction * 4) + 1] = cpu->memory[cpu->PC];
                previousInstructions[(currentInstruction * 4) + 2] = cpu->memory[cpu->PC + 1];
                previousInstructions[(currentInstruction * 4) + 3] = cpu->memory[cpu->PC + 2];

                currentInstruction += 1;

                if (currentInstruction >= MAXINSTRUCTIONS)
                {
                    currentInstruction = 0;
                }

                if (runOnce)
                {
                    notHalted = false;
                    runOnce = false;
                }
            }
            cpu->cycles = 0;
        }

        // render GUI and process inputs

        // currentTime >>=
        if ((currentTime - lastGuiUpdate) >= (unsigned long long)((10000000 / 20)))
        {
            processInput(window, cpu); // input

            // render
            glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            DrawScreen(cpu, shaderProgram, VAO, texture, config.vramStart, config.vramEnd, config.screenW, config.screenH);

            ImGUIFrame(oneInstructionCycle, notHalted, runOnce, runFrame, cpu, PauseOnLine, breakpointActive, MAXINSTRUCTIONS, currentInstruction, previousInstructions);

            // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
            // -------------------------------------------------------------------------------
            glfwSwapBuffers(window);
            glfwPollEvents();

            // std::cout << currentTime - lastGuiUpdate << std::endl;
            lastGuiUpdate = currentTime;
        }
    }

    GraphicsCleanup(VAO, shaderProgram);

    return 0;
}
