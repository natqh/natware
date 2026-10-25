#pragma once
#include "shared_offsets.h"
#include <Windows.h>
#include <string>

namespace offset_dumper {
bool scan(HANDLE proc, uintptr_t base, SIZE_T size, NatwareSharedOffsets& out, std::string& error);
}
