// ═══════════════════════════════════════════════════════════════════════════════
// CPM Debug UI - Painéis ImGui para debugger CP/M
// ═══════════════════════════════════════════════════════════════════════════════
//
// Adicione estas funções ao gui.cpp, antes de ImGUIFrameCPM()

#include "imgui/imgui.h"
#include "intel8080.h"
#include "cpm_bios.h"
#include "cpmdebugstate.h"

// ═══════════════════════════════════════════════════════════════════════════════
// Painel de Registradores e Estado da CPU
// ═══════════════════════════════════════════════════════════════════════════════

static void CPMDebugPanel_Registers(intel8080 *cpu)
{
    ImGui::Begin("CPU State", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (ImGui::CollapsingHeader("Registers", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("PC: 0x%04X  SP: 0x%04X", cpu->PC, cpu->SP);
        ImGui::Text("AF: 0x%02X%02X  BC: 0x%02X%02X", cpu->A, 0, cpu->B, cpu->C);
        ImGui::Text("DE: 0x%02X%02X  HL: 0x%02X%02X", cpu->D, cpu->E, cpu->H, cpu->L);

        ImGui::Separator();
        ImGui::Text("Flags: ");
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(cpu->cf ? 0.2f : 1.0f, 1.0f, 0.2f, 1.0f),
            "C:%d ", cpu->cf ? 1 : 0);
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(cpu->zf ? 0.2f : 1.0f, 1.0f, 0.2f, 1.0f),
            "Z:%d ", cpu->zf ? 1 : 0);
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(cpu->sf ? 0.2f : 1.0f, 1.0f, 0.2f, 1.0f),
            "S:%d ", cpu->sf ? 1 : 0);
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(cpu->pf ? 0.2f : 1.0f, 1.0f, 0.2f, 1.0f),
            "P:%d", cpu->pf ? 1 : 0);
    }

    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Painel CP/M State (Drive, User, DMA)
// ═══════════════════════════════════════════════════════════════════════════════

static void CPMDebugPanel_CPMState(CPMState &cpm, CPMDebugState &dbg)
{
    ImGui::Begin("CP/M State", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (ImGui::CollapsingHeader("Disk & User", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Current Drive: %c:", 'A' + cpm.currentDrive);
        ImGui::Text("Current User: %d", cpm.currentUser);
        ImGui::Text("DMA Address: 0x%04X", cpm.dmaAddress);

        ImGui::Separator();
        ImGui::Text("CCP Active: %s", cpm.ccpRunning ? "YES" : "NO");
        ImGui::Text("CCP Mode: %s", cpm.ccpMode ? "YES" : "NO");
        ImGui::Text("Running: %s", cpm.running ? "YES" : "NO");
    }

    if (ImGui::CollapsingHeader("BDOS Info"))
    {
        std::string bdosStr = dbg.getBdosDisplay();
        ImGui::TextWrapped("%s", bdosStr.c_str());
    }

    if (ImGui::CollapsingHeader("Terminal Input"))
    {
        ImGui::Text("Queue Size: %lu bytes", cpm.terminal.inputQueue.size());

        if (!cpm.terminal.inputQueue.empty())
        {
            ImGui::Text("Next in Queue: 0x%02X", (uint8_t)cpm.terminal.inputQueue[0]);
        }
    }

    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Painel de Histórico de Instruções
// ═══════════════════════════════════════════════════════════════════════════════

static void CPMDebugPanel_InstructionHistory(CPMDebugState &dbg)
{
    ImGui::Begin("Instruction History", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("Total Instructions: %d / %d", dbg.totalInstructions, dbg.MAXINSTR);
    ImGui::Text("Total Cycles: %lu", dbg.totalCycles);

    if (dbg.instructionsPerSecond > 0)
    {
        ImGui::Text("Speed: ~%d instr/sec", dbg.instructionsPerSecond);
    }

    ImGui::Separator();
    ImGui::Text("=== Last 10 Instructions ===");

    ImGui::BeginChild("instruction_history", ImVec2(0, 300), true);

    for (int k = 0; k < 10 && k < dbg.totalInstructions; k++)
    {
        int idx = dbg.totalInstructions - k - 1;
        std::string display = dbg.getInstructionDisplay(idx);

        // Destaca a instrução atual
        if (idx == dbg.totalInstructions - 1)
        {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", display.c_str());
        }
        else
        {
            ImGui::Text("%s", display.c_str());
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Painel de Memória
// ═══════════════════════════════════════════════════════════════════════════════

static void CPMDebugPanel_Memory(intel8080 *cpu, CPMDebugState &dbg)
{
    ImGui::Begin("Memory Viewer", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    static char memAddrBuf[16] = "0100";
    ImGui::InputText("Address (hex)", memAddrBuf, sizeof(memAddrBuf));
    ImGui::SameLine();
    if (ImGui::Button("Go"))
    {
        dbg.memoryViewAddr = (uint16_t)strtol(memAddrBuf, nullptr, 16);
    }

    ImGui::Separator();

    std::string memDisplay = dbg.getMemoryDisplay(cpu, dbg.memoryViewAddr, 8);

    ImGui::BeginChild("memory_view", ImVec2(0, 300), true);
    ImGui::TextUnformatted(memDisplay.c_str());
    ImGui::EndChild();

    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Painel de Stack
// ═══════════════════════════════════════════════════════════════════════════════

static void CPMDebugPanel_Stack(intel8080 *cpu, CPMDebugState &dbg)
{
    ImGui::Begin("Stack", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    std::string stackDisplay = dbg.getStackDisplay(cpu);

    ImGui::TextUnformatted(stackDisplay.c_str());

    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Painel de Controle de Execução
// ═══════════════════════════════════════════════════════════════════════════════

static void CPMDebugPanel_ExecutionControl(intel8080 *cpu, CPMState &cpm, CPMDebugState &dbg)
{
    ImGui::Begin("Execution Control", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    // ── Run / Step ───────────────────────────────────────────────────────────
    if (ImGui::Button(dbg.notHalted ? "Pause" : "Run", ImVec2(80, 0)))
        dbg.notHalted = !dbg.notHalted;
    ImGui::SameLine();
    if (ImGui::Button("Step", ImVec2(80, 0))) { dbg.runOnce = true; dbg.notHalted = false; }

    ImGui::Separator();

    // ── Breakpoint ───────────────────────────────────────────────────────────
    ImGui::Checkbox("Breakpoint", &dbg.breakpointActive);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputText("Addr##bp", dbg.pauseLine, sizeof(dbg.pauseLine),
                     ImGuiInputTextFlags_CharsHexadecimal);

    ImGui::Separator();

    // ── CPU Speed ────────────────────────────────────────────────────────────
    ImGui::Text("CPU Speed:");
    float mhz = (float)dbg.targetMHz;
    ImGui::SetNextItemWidth(180);
    ImGui::SliderFloat("MHz (0=max)", &mhz, 0.0f, 10.0f, mhz > 0 ? "%.2f MHz" : "Unlimited");
    dbg.targetMHz = (double)mhz;

    ImGui::Separator();

    // ── Drives ───────────────────────────────────────────────────────────────
    ImGui::Text("Drives:");
    const char* driveLetters = "ABCD";
    for (int i = 0; i < 4; i++) {
        bool active = (cpm.currentDrive == i);
        if (active) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
        ImGui::Text("%c: %s", driveLetters[i], cpm.diskDirs[i].c_str());
        if (active) ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // ── Save / Load State ────────────────────────────────────────────────────
    static char savePath[512] = "cpm_state.bin";
    ImGui::SetNextItemWidth(260);
    ImGui::InputText("##savepath", savePath, sizeof(savePath));
    if (ImGui::Button("Save State")) {
        if (SaveCPMState(cpu, cpm, savePath))
            ImGui::OpenPopup("Saved!");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load State")) {
        if (LoadCPMState(cpu, cpm, savePath))
            ImGui::OpenPopup("Loaded!");
    }
    if (ImGui::BeginPopup("Saved!"))  { ImGui::Text("State saved.");  ImGui::EndPopup(); }
    if (ImGui::BeginPopup("Loaded!")) { ImGui::Text("State loaded."); ImGui::EndPopup(); }

    ImGui::Separator();

    // ── Status ───────────────────────────────────────────────────────────────
    ImGui::Text("Status: %s", dbg.notHalted ? "RUNNING" : "PAUSED");
    ImGui::Text("Drive: %c:  User: %d", 'A' + cpm.currentDrive, cpm.currentUser);
    ImGui::Text("Cycles: %lu", dbg.totalCycles);

    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Painel Principal de Debug (consolida todas as infos importantes)
// ═══════════════════════════════════════════════════════════════════════════════

static void CPMDebugPanel_Main(intel8080 *cpu, CPMState &cpm, CPMDebugState &dbg)
{
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("CP/M Debug Console", nullptr);

    if (ImGui::BeginTabBar("DebugTabs"))
    {
        // Tab: CPU State
        if (ImGui::BeginTabItem("CPU"))
        {
            std::string regDisplay = dbg.getRegisterDisplay(cpu);
            ImGui::TextUnformatted(regDisplay.c_str());

            ImGui::Separator();
            ImGui::Text("Stack Pointer: 0x%04X", cpu->SP);
            ImGui::Text("Program Counter: 0x%04X", cpu->PC);

            ImGui::EndTabItem();
        }

        // Tab: CP/M
        if (ImGui::BeginTabItem("CP/M"))
        {
            ImGui::Text("Drive: %c:", 'A' + cpm.currentDrive);
            ImGui::Text("User: %d", cpm.currentUser);
            ImGui::Text("DMA: 0x%04X", cpm.dmaAddress);
            ImGui::Text("CCP Running: %s", cpm.ccpRunning ? "YES" : "NO");
            ImGui::Text("Terminal Queue: %lu bytes", cpm.terminal.inputQueue.size());

            ImGui::EndTabItem();
        }

        // Tab: Instructions
        if (ImGui::BeginTabItem("Instructions"))
        {
            ImGui::Text("Total: %d", dbg.totalInstructions);
            ImGui::BeginChild("instr_list", ImVec2(0, -30), true);

            for (int i = 0; i < 20 && i < dbg.totalInstructions; i++)
            {
                int idx = dbg.totalInstructions - i - 1;
                std::string disp = dbg.getInstructionDisplay(idx);
                ImGui::TextUnformatted(disp.c_str());
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // Tab: Memory
        if (ImGui::BeginTabItem("Memory"))
        {
            static char addr[16] = "0100";
            ImGui::InputText("##memaddr", addr, sizeof(addr), ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::SameLine();
            if (ImGui::Button("View"))
            {
                dbg.memoryViewAddr = strtol(addr, nullptr, 16);
            }

            ImGui::Separator();
            std::string memDisp = dbg.getMemoryDisplay(cpu, dbg.memoryViewAddr, 12);
            ImGui::TextUnformatted(memDisp.c_str());

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Função Principal: Renderiza todos os painéis de debug
// ═══════════════════════════════════════════════════════════════════════════════

void DrawCPMDebugger(intel8080 *cpu, CPMState &cpm, CPMDebugState &dbg)
{
    CPMDebugPanel_Main(cpu, cpm, dbg);
    CPMDebugPanel_ExecutionControl(cpu, cpm, dbg);
}
