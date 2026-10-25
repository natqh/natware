#include "offset_dumper.h"
#include "editor_diagnostics.h"
#include <Psapi.h>
#include <vector>
#include <string>
#include <sstream>
#include <cstring>

#pragma comment(lib, "Psapi.lib")

namespace offset_dumper {

// ── scan context (passed through instead of globals) ──────────────────
struct Ctx {
    HANDLE proc;
    uintptr_t base;
    SIZE_T size;
};

// ── hex pattern → bytes + mask ────────────────────────────────────────
static std::pair<std::vector<char>, std::string> hex_to_pattern(const std::string& hex)
{
    std::vector<char> bytes;
    std::string mask;
    std::istringstream stream(hex);
    std::string tok;
    while (stream >> tok) {
        if (tok == "?" || tok == "??") {
            bytes.push_back(0);
            mask += '?';
        } else {
            bytes.push_back(static_cast<char>(strtol(tok.c_str(), nullptr, 16)));
            mask += 'x';
        }
    }
    return { bytes, mask };
}

// ── pattern scan (single pass, no infinite retry) ─────────────────────
static uintptr_t pattern_scan(const Ctx& ctx, const std::string& hex,
                              bool extract_offset = false,
                              const std::string& offset_type = "dword",
                              int skip = 0)
{
    auto [pat, mask] = hex_to_pattern(hex);
    if (pat.empty()) return 0;

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uintptr_t lo = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    uintptr_t hi = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);

    MEMORY_BASIC_INFORMATION mbi{};
    std::vector<char> buf;
    int matched = 0;

    for (uintptr_t addr = lo; addr < hi; addr += mbi.RegionSize) {
        if (!VirtualQueryEx(ctx.proc, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
            { mbi.RegionSize = 0x1000; continue; }
        if (mbi.State != MEM_COMMIT) continue;
        if (!(mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) continue;

        buf.resize(mbi.RegionSize);
        SIZE_T read = 0;
        if (!ReadProcessMemory(ctx.proc, mbi.BaseAddress, buf.data(), mbi.RegionSize, &read))
            continue;

        const size_t plen = pat.size();
        if (plen > read) continue;
        for (size_t i = 0; i <= read - plen; ++i) {
            bool ok = true;
            for (size_t j = 0; j < plen; ++j) {
                if (mask[j] == 'x' && buf[i + j] != pat[j]) { ok = false; break; }
            }
            if (!ok) continue;

            if (matched < skip) { ++matched; continue; }

            uintptr_t result = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + i;
            if (!extract_offset) return result;

            int32_t rel = 0;
            uintptr_t rel_addr = result + 3;
            if (!ReadProcessMemory(ctx.proc, reinterpret_cast<LPCVOID>(rel_addr), &rel, sizeof(rel), nullptr))
                continue;

            if (offset_type == "byte")
                return result + rel + 7;
            else
                return rel_addr + rel + sizeof(rel);
        }
    }
    return 0;
}

// ── string search in remote process memory ────────────────────────────
static uintptr_t find_string(const Ctx& ctx, const std::string& str)
{
    MEMORY_BASIC_INFORMATION mbi{};
    std::vector<uint8_t> buf;

    for (uintptr_t addr = ctx.base; addr < ctx.base + ctx.size; addr += mbi.RegionSize) {
        if (!VirtualQueryEx(ctx.proc, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
            break;
        if (mbi.State != MEM_COMMIT) continue;
        if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
            continue;
        if (mbi.Protect & PAGE_GUARD) continue;

        buf.resize(mbi.RegionSize);
        SIZE_T read = 0;
        if (!ReadProcessMemory(ctx.proc, mbi.BaseAddress, buf.data(), mbi.RegionSize, &read))
            continue;
        if (read < str.size()) continue;

        for (size_t i = 0; i <= read - str.size(); ++i) {
            if (memcmp(&buf[i], str.data(), str.size()) == 0)
                return reinterpret_cast<uintptr_t>(mbi.BaseAddress) + i;
        }
    }
    return 0;
}

// ── LEA + CALL xref pattern (finds a function that references a string) ──
static uintptr_t xref_scan(const Ctx& ctx, const std::string& str,
                           int opcode, int skip_call_down, int skip_call_up)
{
    uintptr_t string_addr = find_string(ctx, str);
    if (!string_addr) return 0;

    MEMORY_BASIC_INFORMATION mbi{};
    std::vector<uint8_t> buf;
    const size_t call_range = 0x300;

    auto get_rel32 = [](const std::vector<uint8_t>& b, size_t off) -> int32_t {
        return *reinterpret_cast<const int32_t*>(&b[off]);
    };

    auto find_nearby_call = [&](const std::vector<uint8_t>& b, size_t start, size_t range,
                                bool fwd, int skip) -> size_t {
        size_t idx = start, checked = 0, skipped = 0;
        while ((fwd ? idx < b.size() - 5 : idx >= 5) && checked < range) {
            if (b[idx] == 0xE8) {
                if (static_cast<int>(skipped++) < skip) { idx += fwd ? 1 : -1; ++checked; continue; }
                return idx;
            }
            idx += fwd ? 1 : -1;
            ++checked;
        }
        return 0;
    };

    for (uintptr_t addr = ctx.base; addr < ctx.base + ctx.size; addr += mbi.RegionSize) {
        if (!VirtualQueryEx(ctx.proc, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
            break;
        if (!(mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))))
            continue;

        buf.resize(mbi.RegionSize);
        SIZE_T read = 0;
        if (!ReadProcessMemory(ctx.proc, mbi.BaseAddress, buf.data(), mbi.RegionSize, &read))
            continue;

        for (size_t i = 0; i + 7 < read; ++i) {
            if (opcode ? (buf[i] != static_cast<uint8_t>(opcode)) : false)
                continue;

            int32_t disp = get_rel32(buf, i + 3);
            uintptr_t target = addr + i + 7 + disp;
            if (target != string_addr) continue;

            size_t lea_off = i;

            if (skip_call_up > 0) {
                size_t c = find_nearby_call(buf, lea_off, call_range, false, skip_call_up);
                if (c) return addr + c + 5 + get_rel32(buf, c + 1);
            }
            if (skip_call_down > 0) {
                size_t c = find_nearby_call(buf, lea_off + 7, call_range, true, skip_call_down);
                if (c) return addr + c + 5 + get_rel32(buf, c + 1);
            }
            return 0;
        }
    }
    return 0;
}

// ── opcode lookup table scan ──────────────────────────────────────────
static uintptr_t find_opcode_table(const Ctx& ctx)
{
    const std::string hex = "42 0F B6 8C 30 ? ? ? ?";
    auto [pat, mask] = hex_to_pattern(hex);

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uintptr_t lo = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    uintptr_t hi = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);

    MEMORY_BASIC_INFORMATION mbi{};
    std::vector<char> buf;

    for (uintptr_t addr = lo; addr < hi; addr += mbi.RegionSize) {
        if (!VirtualQueryEx(ctx.proc, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
            { mbi.RegionSize = 0x1000; continue; }
        if (mbi.State != MEM_COMMIT) continue;
        if (!(mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) continue;

        buf.resize(mbi.RegionSize);
        SIZE_T read = 0;
        if (!ReadProcessMemory(ctx.proc, mbi.BaseAddress, buf.data(), mbi.RegionSize, &read))
            continue;

        for (size_t i = 0; i + pat.size() <= read; ++i) {
            bool ok = true;
            for (size_t j = 0; j < pat.size(); ++j) {
                if (mask[j] == 'x' && buf[i + j] != pat[j]) { ok = false; break; }
            }
            if (!ok) continue;

            uint32_t disp = 0;
            uintptr_t instr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + i;
            if (!ReadProcessMemory(ctx.proc, reinterpret_cast<LPCVOID>(instr + 5), &disp, sizeof(disp), nullptr))
                continue;
            return static_cast<uintptr_t>(disp);
        }
    }
    return 0;
}

// ── patterns (from yubx_dump Patterns.hpp) ────────────────────────────
namespace pat {
    const char* Task_Defer          = "48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 81 EC ? ? ? ? 48 8B F9 80 3D";
    const char* PushInstance        = "48 89 5C 24 08 57 48 83 EC ? 48 8B FA 48 8B D9 E8 ? ? ? ? 84 C0 74 ? 48 8B D7 48 8B CB 48 8B 5C 24";
    const char* Luau_Execute        = "80 79 06 00 0F 85 ? ? ? ? E9 ? ? ? ?";
    const char* LuaO_nilobject      = "48 8d 3d ? ? ? ? 48 3b d7";
    const char* LuaH_Dummynode      = "48 8d 3d ? ? ? ? 48 8B D9 48 39";
    const char* KTable              = "48 8d 0d ? ? ? ? 48 8d 54 24 ? 48 8b 04 c1";
    const char* FakeDataModel       = "48 8D 1D ? ? ? ? 8B 07";
    const char* LuaD_throw          = "48 83 EC ? 44 8B C2 48 8B D1 48 8D 4C 24";
    const char* IdentityPtr         = "48 8B 0D ? ? ? ? E8 ? ? ? ? 48 8B F0 48 89 45 ? 44 0F 10 00 44 0F 11 45 ? F2 0F 10 78 ? F2 0F 11 7D ? 4C 8B 70 ? 4C 89 75 ? 4C 8B 78 ? 4C 89 7D ? 0F 10 55 ? 66 0F 15 F6 33 FF";
    const char* GetIdentityStruct   = "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B F9 E8 ? ? ? ? 90";
    const char* GetProperty         = "40 57 48 8B 39 4C 8B 41 ? 49 3B F8 75 ? 45 33 C9 41 8B C9";
    const char* FireMouseClick      = "48 89 5C 24 ? 55 56 57 48 83 EC 70 49 8B F8 48 8B F1";
    const char* FireRightMouseClick = "48 89 5C 24 ? 55 56 57 48 83 EC 70 49 8B F8 48 8B F1";
    const char* FireMouseHoverEnter = "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 70 48 8B FA 48 8B F1 33 ED 89 AC 24 88";
    const char* FireMouseHoverLeave = "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 70 48 8B FA 48 8B F1 33 ED 89 AC 24 ? ? ? ? 48 8B 01 48 8B 80 ? ? ? ? FF 15 ? ? ? ?";
    const char* FireTouchInterest   = "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B 79 ? 41 0F B6 F1";
    const char* FireProximityPrompt = "48 89 5C 24 ? 55 56 57 41 56 41 57 48 8D 6C 24 ? 48 81 EC ? ? ? ? 48 8B F9 E8 ? ? ? ?";
    const char* RawScheduler        = "48 89 05 ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B 0D ? ? ? ? EB ? 39 05 ? ? ? ? 7E ? 48 8D 0D ? ? ? ? E8 ? ? ? ? 83 3D ? ? ? ? ? 75 ? B9 ? ? ? ? E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 48 89 44 24 ? 48 8B C8 E8 ? ? ? ? 90 48 89 05 ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B 0D ? ? ? ? 8B D3";
    const char* TaskSchedulerFps    = "4C 8D 05 5E EC DA 02";
    const char* GetValues           = "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC ? ? ? ? 45 0F B6 D1 4D 8B F8 48 63 FA 48 8B F1";
}

// ── helper: absolute → RVA ────────────────────────────────────────────
static uintptr_t to_rva(uintptr_t abs, uintptr_t base) { return abs ? abs - base : 0; }

// ── public entry point ────────────────────────────────────────────────
bool scan(HANDLE proc, uintptr_t base, SIZE_T size,
          NatwareSharedOffsets& out, std::string& error)
{
    memset(&out, 0, sizeof(out));
    out.magic   = NATWARE_OFFSETS_MAGIC;
    out.version = NATWARE_OFFSETS_VERSION;

    Ctx ctx{ proc, base, size };

    editor_diagnostics::write("info", "dumper.start", {{"base", base}, {"size", size}});

    // Core — xref scans
    out.rva_Print                 = to_rva(xref_scan(ctx, "Current identity is %d", 0x48, 1, 0), base);
    out.rva_GetLuaStateForInstance= to_rva(xref_scan(ctx, "Script Start", 0x4C, 0, 2), base);
    out.rva_LuaVMLoad            = to_rva(xref_scan(ctx, "oldResult, moduleRef", 0x48, 6, 0), base);

    // OpcodeLookupTable (returns RVA directly)
    out.rva_OpcodeLookupTable     = find_opcode_table(ctx);

    // Pattern scans — direct matches (absolute, convert to RVA)
    out.rva_TaskDefer             = to_rva(pattern_scan(ctx, pat::Task_Defer), base);
    out.rva_luaD_throw            = to_rva(pattern_scan(ctx, pat::LuaD_throw), base);
    out.rva_CastArgs              = to_rva(pattern_scan(ctx, pat::GetValues), base);
    out.rva_Luau_Execute          = to_rva(pattern_scan(ctx, pat::Luau_Execute), base);
    out.rva_PushInstance           = to_rva(pattern_scan(ctx, pat::PushInstance), base);
    out.rva_GetProperty           = to_rva(pattern_scan(ctx, pat::GetProperty), base);
    out.rva_FireMouseClick        = to_rva(pattern_scan(ctx, pat::FireMouseClick), base);
    out.rva_FireRightMouseClick   = to_rva(pattern_scan(ctx, pat::FireRightMouseClick, false, "dword", 1), base);
    out.rva_FireMouseHoverEnter   = to_rva(pattern_scan(ctx, pat::FireMouseHoverEnter), base);
    out.rva_FireMouseHoverLeave   = to_rva(pattern_scan(ctx, pat::FireMouseHoverLeave), base);
    out.rva_FireTouchInterest     = to_rva(pattern_scan(ctx, pat::FireTouchInterest), base);
    out.rva_FireProximityPrompt   = to_rva(pattern_scan(ctx, pat::FireProximityPrompt), base);
    out.rva_GetTlsPointer         = to_rva(pattern_scan(ctx, pat::GetIdentityStruct), base);

    // Pattern scans — with offset extraction (already absolute, convert to RVA)
    out.rva_LuaO_NilObject        = to_rva(pattern_scan(ctx, pat::LuaO_nilobject, true, "unk"), base);
    out.rva_LuaH_DummyNode        = to_rva(pattern_scan(ctx, pat::LuaH_Dummynode, true, "unk"), base);
    out.rva_k_table               = to_rva(pattern_scan(ctx, pat::KTable, true, "unk"), base);
    out.rva_IdentityPtr           = to_rva(pattern_scan(ctx, pat::IdentityPtr, true, "dword"), base);
    out.rva_TaskScheduler         = to_rva(pattern_scan(ctx, pat::RawScheduler, true), base);
    out.rva_TaskSchedulerTargetFps= to_rva(pattern_scan(ctx, pat::TaskSchedulerFps, true, "dword"), base);

    // FakeDataModelPointer = resolved_address + 8
    uintptr_t fake_dm = pattern_scan(ctx, pat::FakeDataModel, true, "unk");
    out.rva_FakeDataModelPointer  = fake_dm ? to_rva(fake_dm + 8, base) : 0;

    int found = 0, total = 0;
    const uintptr_t* fields = reinterpret_cast<const uintptr_t*>(
        reinterpret_cast<const uint8_t*>(&out) + offsetof(NatwareSharedOffsets, rva_Print));
    const int field_count = (sizeof(NatwareSharedOffsets) - offsetof(NatwareSharedOffsets, rva_Print)) / sizeof(uintptr_t);
    for (int i = 0; i < field_count; ++i) {
        ++total;
        if (fields[i]) ++found;
    }

    editor_diagnostics::write("info", "dumper.done", {{"found", found}, {"total", total}});

    const int critical_minimum = 8;
    if (found < critical_minimum) {
        error = "Offset scan found only " + std::to_string(found) + "/" + std::to_string(total)
              + " offsets (need at least " + std::to_string(critical_minimum) + ")";
        return false;
    }
    return true;
}

}
