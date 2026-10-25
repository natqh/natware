#pragma once
#include <cstdint>

#define NATWARE_OFFSETS_MAGIC    0x4E415457u
#define NATWARE_OFFSETS_VERSION  1u
#define NATWARE_OFFSETS_SHM_NAME L"Local\\natware_offsets"

struct NatwareSharedOffsets {
    uint32_t magic;
    uint32_t version;

    uintptr_t rva_Print;
    uintptr_t rva_OpcodeLookupTable;
    uintptr_t rva_GetLuaStateForInstance;
    uintptr_t rva_LuaVMLoad;
    uintptr_t rva_TaskDefer;
    uintptr_t rva_luaD_throw;
    uintptr_t rva_ScriptContextResume;
    uintptr_t rva_CastArgs;

    uintptr_t rva_Luau_Execute;
    uintptr_t rva_LuaO_NilObject;
    uintptr_t rva_LuaH_DummyNode;

    uintptr_t rva_FakeDataModelPointer;
    uintptr_t rva_k_table;

    uintptr_t rva_PushInstance;

    uintptr_t rva_GetProperty;
    uintptr_t rva_HashLookup;

    uintptr_t rva_FireTouchInterest;
    uintptr_t rva_DisconnectConnect;

    uintptr_t rva_FireMouseClick;
    uintptr_t rva_FireRightMouseClick;
    uintptr_t rva_FireMouseHoverEnter;
    uintptr_t rva_FireMouseHoverLeave;
    uintptr_t rva_FireProximityPrompt;
    uintptr_t rva_TaskSchedulerTargetFps;

    uintptr_t rva_IdentityPtr;
    uintptr_t rva_GetTlsPointer;

    uintptr_t rva_TaskScheduler;
    uintptr_t rva_GetCapabilities;

    uintptr_t rva_TaskDesynchronize;
    uintptr_t rva_TaskSynchronize;
};
