// ═══════════════════════════════════════════════════════════════════════════════
// CPM Debug UI - Painéis ImGui para debugger CP/M
// Incluído diretamente em gui.cpp — tem acesso a mem_edit e DISSAMBLER_STATES
// ═══════════════════════════════════════════════════════════════════════════════

#include "imgui/imgui.h"
#include "intel8080.h"
#include "cpm_bios.h"
#include "cpm_debug_state.h"
#include <algorithm>

// ─── Helpers visuais ──────────────────────────────────────────────────────────

static ImVec4 FlagColor(bool on)
{
    return on ? ImVec4(0.20f, 1.00f, 0.20f, 1.0f)
              : ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
}

static void FlagBadge(const char *label, bool on)
{
    ImGui::TextColored(FlagColor(on), "[%s]", label);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s: %s", label, on ? "SET" : "CLEAR");
    ImGui::SameLine();
}

// ─── Tab: Registradores ───────────────────────────────────────────────────────

static void DrawRegistersTab(intel8080 *cpu)
{
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f),
                       "PC: 0x%04X    SP: 0x%04X", cpu->PC, cpu->SP);

    ImGui::Separator();

    if (ImGui::BeginTable("##regs", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn(" A  ");
        ImGui::TableSetupColumn(" BC ");
        ImGui::TableSetupColumn(" DE ");
        ImGui::TableSetupColumn(" HL ");
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text(" %02X  ", cpu->A);
        ImGui::TableNextColumn(); ImGui::Text("%02X %02X", cpu->B, cpu->C);
        ImGui::TableNextColumn(); ImGui::Text("%02X %02X", cpu->D, cpu->E);
        ImGui::TableNextColumn(); ImGui::Text("%02X %02X", cpu->H, cpu->L);

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Text("Flags: ");
    ImGui::SameLine();
    FlagBadge("C",  cpu->cf);
    FlagBadge("Z",  cpu->zf);
    FlagBadge("S",  cpu->sf);
    FlagBadge("P",  cpu->pf);
    FlagBadge("AC", cpu->acf);
    ImGui::NewLine();

    ImGui::Separator();

    // Instrução atual com disassembler
    uint8_t op = cpu->memory[cpu->PC];
    uint8_t p1 = cpu->memory[cpu->PC + 1];
    uint8_t p2 = cpu->memory[cpu->PC + 2];
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f),
                       ">> %04X: %02X %02X %02X   %s",
                       cpu->PC, op, p1, p2, DISSAMBLER_STATES[op]);

    ImGui::Spacing();
    ImGui::Text("Interrupts: ");
    ImGui::SameLine();
    ImGui::TextColored(
        cpu->interrupts ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                        : ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
        "%s", cpu->interrupts ? "ENABLED" : "DISABLED");
}

// ─── Tab: Estado CP/M ─────────────────────────────────────────────────────────

static void DrawCPMTab(CPMState &cpm, CPMDebugState &dbg)
{
    if (ImGui::BeginTable("##drives", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_ScrollY,
            ImVec2(0, 130)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Dr", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("Tipo", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Diretório / Imagem", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < MAX_DRIVES; i++)
        {
            bool hasDsk = cpm.diskImages[i] && cpm.diskImages[i]->isOpen();
            bool hasDir = !cpm.diskDirs[i].empty();
            if (!hasDsk && !hasDir) continue;
            bool active = (cpm.currentDrive == i);

            ImGui::TableNextRow();
            if (active)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    ImGui::GetColorU32(ImVec4(0.15f, 0.4f, 0.15f, 0.5f)));

            ImGui::TableNextColumn();
            ImGui::Text("%c%s", 'A' + i, active ? "<" : ":");

            ImGui::TableNextColumn();
            if (hasDsk)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "DSK");
            else
                ImGui::TextDisabled("DIR");

            ImGui::TableNextColumn();
            const char *label = hasDsk ? cpm.diskImages[i]->path.c_str()
                                       : cpm.diskDirs[i].c_str();
            ImGui::TextUnformatted(label);
        }
        ImGui::EndTable();
    }

    ImGui::Separator();

    ImGui::Text("User:    %d", cpm.currentUser);
    ImGui::Text("DMA:     0x%04X", cpm.dmaAddress);

    auto statusColor = [](bool v) -> ImVec4 {
        return v ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    };

    ImGui::Text("CCP:     "); ImGui::SameLine();
    ImGui::TextColored(statusColor(cpm.ccpRunning), "%s", cpm.ccpRunning ? "RUNNING" : "OFF");

    ImGui::Text("Running: "); ImGui::SameLine();
    ImGui::TextColored(statusColor(cpm.running), "%s", cpm.running ? "YES" : "NO");

    ImGui::Separator();
    size_t queueSz = cpm.terminal.inputQueue.size();
    ImGui::Text("Input Queue: %zu byte%s", queueSz, queueSz == 1 ? "" : "s");
    if (!cpm.terminal.inputQueue.empty())
    {
        uint8_t next = (uint8_t)cpm.terminal.inputQueue[0];
        ImGui::Text("Next char:   0x%02X  '%c'",
                    next, (next >= 0x20 && next < 0x7F) ? (char)next : '?');
    }

    ImGui::Separator();
    ImGui::Text("Último BDOS: ");
    ImGui::SameLine();
    if (dbg.lastBdosCall < BDOS_FUNCTION_COUNT)
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                           "#%02X  %s", dbg.lastBdosCall,
                           BDOS_FUNCTION_NAMES[dbg.lastBdosCall]);
    else
        ImGui::TextDisabled("(nenhum)");

    // ── Periféricos I/O ──────────────────────────────────────────────────
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Periféricos I/O");

    // Reader (fn 3)
    static char readerBuf[512] = {};
    // Keep buffer in sync when cpm.readerPath changes externally.
    if (cpm.readerPath.size() < sizeof(readerBuf) &&
        std::strncmp(readerBuf, cpm.readerPath.c_str(), sizeof(readerBuf)) != 0)
        std::strncpy(readerBuf, cpm.readerPath.c_str(), sizeof(readerBuf) - 1);

    ImGui::Text("RDR (fn 3):");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-80.0f);
    if (ImGui::InputText("##rdr_path", readerBuf, sizeof(readerBuf),
                         ImGuiInputTextFlags_EnterReturnsTrue))
        cpm.readerPath = readerBuf;
    ImGui::SameLine();
    if (cpm.readerFp)
    {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "OPEN");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fechar o arquivo reader");
        if (ImGui::Button("X##rdr_close"))
        {
            fclose(cpm.readerFp);
            cpm.readerFp = nullptr;
        }
    }
    else
    {
        ImGui::TextDisabled("fechado");
    }

    // Punch (fn 4) — show path + flush/close button
    ImGui::Text("PUN (fn 4):");
    ImGui::SameLine();
    if (cpm.punchFp)
    {
        const std::string &d = !cpm.diskDirs[0].empty() ? cpm.diskDirs[0] : ".";
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s/CPM.PUN",
                           d.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Flush##pun"))  fflush(cpm.punchFp);
        ImGui::SameLine();
        if (ImGui::Button("Fechar##pun"))
        {
            fclose(cpm.punchFp);
            cpm.punchFp = nullptr;
        }
    }
    else
    {
        ImGui::TextDisabled("(inativo — abre ao primeiro byte enviado)");
    }

    // Printer (fn 5)
    ImGui::Text("LST (fn 5):");
    ImGui::SameLine();
    if (cpm.printerFp)
    {
        const std::string &d = !cpm.diskDirs[0].empty() ? cpm.diskDirs[0] : ".";
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s/CPM.LST",
                           d.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Flush##lst"))  fflush(cpm.printerFp);
        ImGui::SameLine();
        if (ImGui::Button("Fechar##lst"))
        {
            fclose(cpm.printerFp);
            cpm.printerFp = nullptr;
        }
    }
    else
    {
        ImGui::TextDisabled("(inativo — abre ao primeiro byte enviado)");
    }
}

// ─── Helpers para disassembler prospectivo ────────────────────────────────────

static int InstrSize(uint8_t op)
{
    const char *mn = DISSAMBLER_STATES[op];
    if (strstr(mn, "D16") || strstr(mn, "A16")) return 3;
    if (strstr(mn, "D8"))                        return 2;
    return 1;
}

// Retorna a cor ImGui para uma instrução (por categoria).
static ImVec4 InstrColor(uint8_t op)
{
    const char *mn = DISSAMBLER_STATES[op];
    if (op == 0x76)                          return ImVec4(1.0f, 0.25f, 0.25f, 1.0f); // HLT
    if (strncmp(mn, "RET", 3) == 0 ||
        mn[0] == 'R')                        return ImVec4(1.0f, 0.55f, 0.55f, 1.0f); // RET/Rcc
    if (strncmp(mn, "CALL", 4) == 0 ||
        mn[0] == 'C')                        return ImVec4(1.0f, 0.75f, 0.25f, 1.0f); // CALL/Ccc
    if (strncmp(mn, "JMP", 3) == 0 ||
        mn[0] == 'J')                        return ImVec4(1.0f, 1.0f, 0.35f, 1.0f);  // JMP/Jcc
    if (strncmp(mn, "MOV", 3) == 0 ||
        strncmp(mn, "MVI", 3) == 0 ||
        strncmp(mn, "LXI", 3) == 0 ||
        strncmp(mn, "LDA", 3) == 0 ||
        strncmp(mn, "STA", 3) == 0 ||
        strncmp(mn, "LDAX", 4) == 0 ||
        strncmp(mn, "STAX", 4) == 0 ||
        strncmp(mn, "LHLD", 4) == 0 ||
        strncmp(mn, "SHLD", 4) == 0)        return ImVec4(0.75f, 0.85f, 1.0f, 1.0f);  // mov/load/store
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);                                             // default
}

// Formata operando de 16 bits com fallback para símbolo.
static void Fmt16(char *out, size_t sz, uint16_t imm, const CPMDebugState *dbg)
{
    const char *sym = dbg ? dbg->resolveSymbol(imm) : nullptr;
    if (sym) snprintf(out, sz, "%s", sym);
    else     snprintf(out, sz, "0x%04X", imm);
}

// Formata instrução com operandos resolvidos e nomes de símbolo quando disponíveis.
// Ex: "JMP BDOS", "CALL 0x0105", "MVI A,0x3E"
static void FormatInstrWithOperands(char *buf, size_t sz,
                                    uint16_t addr, const uint8_t *mem,
                                    const CPMDebugState *dbg = nullptr)
{
    uint8_t op = mem[addr];
    uint8_t p1 = mem[(uint16_t)(addr + 1)];
    uint8_t p2 = mem[(uint16_t)(addr + 2)];
    const char *mn = DISSAMBLER_STATES[op];

    if (strstr(mn, "D16") || strstr(mn, "A16"))
    {
        uint16_t imm = (uint16_t)(p1 | (p2 << 8));
        char tmp[32];
        Fmt16(tmp, sizeof(tmp), imm, dbg);
        const char *tag = strstr(mn, "D16") ? "D16" : "A16";
        size_t pre = (size_t)(tag - mn);
        snprintf(buf, sz, "%.*s%s", (int)pre, mn, tmp);
    }
    else if (strstr(mn, "D8"))
    {
        char tmp[16];
        snprintf(tmp, sizeof(tmp), "0x%02X", p1);
        const char *tag = strstr(mn, "D8");
        size_t pre = (size_t)(tag - mn);
        snprintf(buf, sz, "%.*s%s", (int)pre, mn, tmp);
    }
    else
    {
        snprintf(buf, sz, "%s", mn);
    }
}

// ─── Tab: Histórico de Instruções ─────────────────────────────────────────────

static void DrawInstructionsTab(intel8080 *cpu, CPMDebugState &dbg)
{
    ImGui::Text("Total: %d    Ciclos: %llu",
                dbg.totalInstructions, (unsigned long long)dbg.totalCycles);
    if (dbg.instructionsPerSecond > 0)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f),
                           "   ~%d instr/s", dbg.instructionsPerSecond);
    }

    ImGui::Separator();

    // ── Próximas instruções (prospectivo) ─────────────────────────────────
    {
        const char *pcSym = dbg.resolveSymbol(cpu->PC);
        if (pcSym)
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f),
                               "Próximas instruções (%s / PC=0x%04X):", pcSym, cpu->PC);
        else
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f),
                               "Próximas instruções (PC=0x%04X):", cpu->PC);
    }

    ImGui::BeginChild("##ahead_scroll", ImVec2(0, 180), true,
                      ImGuiWindowFlags_HorizontalScrollbar);

    static const int AHEAD_COUNT = 24;
    uint16_t addr = cpu->PC;
    for (int i = 0; i < AHEAD_COUNT; i++)
    {
        // Label marker quando o endereço tem símbolo (exceto PC já mostrado no header)
        if (i > 0)
        {
            const char *sym = dbg.resolveSymbol(addr);
            if (sym)
            {
                ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f),
                                   "  ─── %s ────────────────────", sym);
            }
        }

        uint8_t op   = cpu->memory[addr];
        int     size = InstrSize(op);

        char mnem[56];
        FormatInstrWithOperands(mnem, sizeof(mnem), addr, cpu->memory, &dbg);

        char line[88];
        switch (size)
        {
        case 3:
            snprintf(line, sizeof(line), "%04X: %02X %02X %02X  %-24s",
                     addr, op,
                     cpu->memory[(uint16_t)(addr+1)],
                     cpu->memory[(uint16_t)(addr+2)], mnem);
            break;
        case 2:
            snprintf(line, sizeof(line), "%04X: %02X %02X      %-24s",
                     addr, op,
                     cpu->memory[(uint16_t)(addr+1)], mnem);
            break;
        default:
            snprintf(line, sizeof(line), "%04X: %02X         %-24s",
                     addr, op, mnem);
            break;
        }

        if (i == 0)
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), ">> %s", line);
        else
            ImGui::TextColored(InstrColor(op), "   %s", line);

        addr = (uint16_t)(addr + size);
    }

    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.5f, 1.0f), "Histórico (mais recente primeiro):");

    // ── Histórico ─────────────────────────────────────────────────────────
    ImGui::BeginChild("##instr_scroll", ImVec2(0, -4), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    int count = std::min(dbg.totalInstructions, 128);
    for (int i = 0; i < count; i++)
    {
        int idx = dbg.totalInstructions - i - 1;
        std::string disp = dbg.getInstructionDisplay(idx);
        if (i == 0)
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "> %s", disp.c_str());
        else
            ImGui::TextUnformatted(disp.c_str());
    }

    ImGui::EndChild();
}

// ─── Tab: Memória ─────────────────────────────────────────────────────────────

static void DrawMemoryTab(intel8080 *cpu, CPMDebugState &dbg)
{
    static char addrBuf[8] = "0100";

    ImGui::SetNextItemWidth(90);
    ImGui::InputText("##memaddr", addrBuf, sizeof(addrBuf),
                     ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    if (ImGui::Button("Ir"))
    {
        dbg.memoryViewAddr = (uint16_t)strtol(addrBuf, nullptr, 16);
        mem_edit.GotoAddrAndHighlight(dbg.memoryViewAddr, dbg.memoryViewAddr + 1);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("ZP"))
    {
        dbg.memoryViewAddr = 0x0000;
        snprintf(addrBuf, sizeof(addrBuf), "0000");
        mem_edit.GotoAddrAndHighlight(0x0000, 0x0001);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("TPA"))
    {
        dbg.memoryViewAddr = 0x0100;
        snprintf(addrBuf, sizeof(addrBuf), "0100");
        mem_edit.GotoAddrAndHighlight(0x0100, 0x0101);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("BDOS"))
    {
        dbg.memoryViewAddr = 0xF800;
        snprintf(addrBuf, sizeof(addrBuf), "F800");
        mem_edit.GotoAddrAndHighlight(0xF800, 0xF801);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("SP"))
    {
        dbg.memoryViewAddr = cpu->SP;
        snprintf(addrBuf, sizeof(addrBuf), "%04X", cpu->SP);
        mem_edit.GotoAddrAndHighlight(cpu->SP, cpu->SP + 1);
    }

    ImGui::Separator();

    // Hex dump compacto
    ImGui::BeginChild("##hexdump", ImVec2(0, 210), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    std::string dump = dbg.getMemoryDisplay(cpu, dbg.memoryViewAddr, 12);
    ImGui::TextUnformatted(dump.c_str());
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button("Abrir Editor de Memória Completo"))
        mem_edit.Open = true;

    // Editor de memória flutuante
    if (mem_edit.Open)
        mem_edit.DrawWindow("Memory Editor##full", cpu->memory, 0x10000);
}

// ─── Tab: Stack ───────────────────────────────────────────────────────────────

static void DrawStackTab(intel8080 *cpu, CPMDebugState &dbg)
{
    const char *spSym = dbg.resolveSymbol(cpu->SP);
    if (spSym)
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f),
                           "Stack Pointer: 0x%04X  (%s)", cpu->SP, spSym);
    else
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f),
                           "Stack Pointer: 0x%04X", cpu->SP);
    ImGui::Separator();

    if (ImGui::BeginTable("##stack", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Offset");
        ImGui::TableSetupColumn("Endereço");
        ImGui::TableSetupColumn("Valor");
        ImGui::TableSetupColumn("Símbolo");
        ImGui::TableHeadersRow();

        for (int i = 0; i < 12; i++)
        {
            uint16_t stackAddr = (uint16_t)(cpu->SP + i * 2);
            uint16_t val = (uint16_t)((cpu->memory[(uint16_t)(stackAddr+1)] << 8) |
                                       cpu->memory[stackAddr]);
            const char *sym = dbg.resolveSymbol(val);

            ImGui::TableNextRow();
            if (i == 0)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    ImGui::GetColorU32(ImVec4(0.2f, 0.45f, 0.2f, 0.45f)));

            ImVec4 valColor = (i == 0) ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                                       : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

            ImGui::TableNextColumn(); ImGui::Text("SP+%02d", i * 2);
            ImGui::TableNextColumn(); ImGui::Text("0x%04X", stackAddr);
            ImGui::TableNextColumn(); ImGui::TextColored(valColor, "0x%04X", val);
            ImGui::TableNextColumn();
            if (sym)
                ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "%s", sym);
            else
                ImGui::TextDisabled("—");
        }

        ImGui::EndTable();
    }
}

// ─── Tab: Símbolos ────────────────────────────────────────────────────────────

static void DrawSymbolsTab(CPMDebugState &dbg)
{
    // ── Carregamento ──────────────────────────────────────────────────────
    ImGui::SetNextItemWidth(-110.0f);
    ImGui::InputText("##sympath", dbg.symbolFilePath, sizeof(dbg.symbolFilePath));
    ImGui::SameLine();
    if (ImGui::Button("Carregar##sym"))
        dbg.loadSymbols(dbg.symbolFilePath);
    ImGui::SameLine();
    if (!dbg.symbols.empty() && ImGui::SmallButton("Limpar##sym"))
        dbg.symbols.clear();

    if (dbg.symbols.empty())
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Nenhum símbolo carregado.");
        ImGui::TextDisabled("Formato do arquivo (um por linha):");
        ImGui::TextDisabled("  F800 BDOS");
        ImGui::TextDisabled("  E800 CCP");
        ImGui::TextDisabled("  0100 TPA_START");
        ImGui::TextDisabled("  ; linhas com ; ou # são comentários");
        return;
    }

    ImGui::Text("%d símbolo%s carregado%s",
                (int)dbg.symbols.size(),
                dbg.symbols.size() == 1 ? "" : "s",
                dbg.symbols.size() == 1 ? "" : "s");

    // ── Filtro ────────────────────────────────────────────────────────────
    static char filterBuf[64] = {};
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##symfilter", "filtrar por nome ou endereço...",
                             filterBuf, sizeof(filterBuf));

    ImGui::Separator();

    // ── Tabela (ordenada por endereço) ────────────────────────────────────
    ImGui::BeginChild("##sym_scroll", ImVec2(0, -4), false);

    // Coletar e ordenar
    std::vector<std::pair<uint16_t, const std::string *>> sorted;
    sorted.reserve(dbg.symbols.size());
    for (auto &kv : dbg.symbols)
        sorted.push_back({kv.first, &kv.second});
    std::sort(sorted.begin(), sorted.end(),
              [](auto &a, auto &b){ return a.first < b.first; });

    std::string flt(filterBuf);
    // lowercase para comparação case-insensitive
    for (char &c : flt) c = (char)tolower((unsigned char)c);

    if (ImGui::BeginTable("##sym_table", 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0, -4)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Endereço", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Nome");
        ImGui::TableHeadersRow();

        for (auto &[addr, namePtr] : sorted)
        {
            if (!flt.empty())
            {
                // checar se nome ou endereço hex contém o filtro
                std::string nameLow(*namePtr);
                for (char &c : nameLow) c = (char)tolower((unsigned char)c);
                char addrHex[8];
                snprintf(addrHex, sizeof(addrHex), "%04x", addr);
                if (nameLow.find(flt) == std::string::npos &&
                    std::string(addrHex).find(flt) == std::string::npos)
                    continue;
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "0x%04X", addr);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(namePtr->c_str());
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
}

// ─── Tab: BDOS Log ────────────────────────────────────────────────────────────

static void DrawBdosTab(CPMDebugState &dbg)
{
    ImGui::Text("Histórico de chamadas BDOS (mais recente primeiro):");
    ImGui::Separator();

    ImGui::BeginChild("##bdos_log", ImVec2(0, -4), false);

    if (dbg.bdosLogCount == 0)
    {
        ImGui::TextDisabled("Nenhuma chamada BDOS registrada ainda.");
    }
    else if (ImGui::BeginTable("##bdos_table", 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0, -4)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Fn#",  ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Função BDOS");
        ImGui::TableHeadersRow();

        for (int i = 0; i < dbg.bdosLogCount; i++)
        {
            int idx = (dbg.bdosLogHead - 1 - i + CPMDebugState::BDOS_LOG_MAX * 2)
                      % CPMDebugState::BDOS_LOG_MAX;
            auto &e = dbg.bdosLog[idx];
            if (!e.valid)
                break;

            ImGui::TableNextRow();
            if (i == 0)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    ImGui::GetColorU32(ImVec4(0.2f, 0.4f, 0.2f, 0.45f)));

            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%02X", e.fn);

            ImGui::TableNextColumn();
            const char *name = (e.fn < BDOS_FUNCTION_COUNT)
                               ? BDOS_FUNCTION_NAMES[e.fn] : "???";
            ImGui::TextUnformatted(name);
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
}

// ─── Janela Principal de Debug (tab bar) ─────────────────────────────────────

static void CPMDebugPanel_Main(intel8080 *cpu, CPMState &cpm, CPMDebugState &dbg)
{
    ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin("CP/M Debugger");

    if (ImGui::BeginTabBar("##debug_tabs"))
    {
        if (ImGui::BeginTabItem("Registradores"))
        {
            DrawRegistersTab(cpu);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("CP/M State"))
        {
            DrawCPMTab(cpm, dbg);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Instruções"))
        {
            DrawInstructionsTab(cpu, dbg);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Memória"))
        {
            DrawMemoryTab(cpu, dbg);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stack"))
        {
            DrawStackTab(cpu, dbg);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("BDOS Log"))
        {
            DrawBdosTab(dbg);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Símbolos"))
        {
            DrawSymbolsTab(dbg);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ─── Controle de Execução ─────────────────────────────────────────────────────

static void CPMDebugPanel_ExecutionControl(intel8080 *cpu, CPMState &cpm, CPMDebugState &dbg)
{
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Execution Control", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    // Run / Step / Reset
    if (ImGui::Button(dbg.notHalted ? "  Pause  " : "   Run   ", ImVec2(90, 0)))
        dbg.notHalted = !dbg.notHalted;
    ImGui::SameLine();
    if (ImGui::Button("Step", ImVec2(65, 0)))
    {
        dbg.runOnce   = true;
        dbg.notHalted = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset", ImVec2(65, 0)))
    {
        cpu->PC                = 0;
        dbg.totalCycles        = 0;
        dbg.totalInstructions  = 0;
        dbg.currentInstruction = 0;
        dbg.bdosLogHead        = 0;
        dbg.bdosLogCount       = 0;
    }

    ImGui::Separator();

    // Breakpoint
    ImGui::Checkbox("Breakpoint", &dbg.breakpointActive);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputText("Addr##bp", dbg.pauseLine, sizeof(dbg.pauseLine),
                     ImGuiInputTextFlags_CharsHexadecimal);
    if (dbg.breakpointActive)
    {
        dbg.breakpointAddr = (uint16_t)strtol(dbg.pauseLine, nullptr, 16);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.1f, 1.0f), "@ 0x%04X",
                           dbg.breakpointAddr);
    }

    ImGui::Separator();

    // Velocidade da CPU
    float mhz = (float)dbg.targetMHz;
    ImGui::SetNextItemWidth(190);
    ImGui::SliderFloat("MHz", &mhz, 0.0f, 10.0f,
                       mhz > 0.001f ? "%.2f MHz" : "Unlimited");
    dbg.targetMHz = (double)mhz;

    ImGui::Separator();

    // Save / Load State
    static char savePath[512] = "cpm_state.bin";
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("##savepath", savePath, sizeof(savePath));
    ImGui::SameLine();
    if (ImGui::Button("Save"))
        if (SaveCPMState(cpu, cpm, savePath))
            ImGui::OpenPopup("##saved_ok");
    ImGui::SameLine();
    if (ImGui::Button("Load"))
        if (LoadCPMState(cpu, cpm, savePath))
            ImGui::OpenPopup("##loaded_ok");

    if (ImGui::BeginPopup("##saved_ok"))
    {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), " Estado salvo! ");
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##loaded_ok"))
    {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), " Estado carregado! ");
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // Status bar
    ImGui::TextColored(
        dbg.notHalted ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                      : ImVec4(1.0f, 0.6f, 0.1f, 1.0f),
        "%-8s", dbg.notHalted ? "RUNNING" : "PAUSED");
    ImGui::SameLine();
    ImGui::Text("Drive: %c:  User: %d  Ciclos: %llu",
                'A' + cpm.currentDrive, cpm.currentUser,
                (unsigned long long)dbg.totalCycles);

    ImGui::End();
}

// ─── Ponto de Entrada ─────────────────────────────────────────────────────────

void DrawCPMDebugger(intel8080 *cpu, CPMState &cpm, CPMDebugState &dbg)
{
    CPMDebugPanel_Main(cpu, cpm, dbg);
    CPMDebugPanel_ExecutionControl(cpu, cpm, dbg);
}
