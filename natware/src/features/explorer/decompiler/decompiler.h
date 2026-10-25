#pragma once
#include <cstdint>
#include <string>
#include <windows.h>

namespace decompiler
{
    enum script_type
    {
        LocalScript = 0,
        ModuleScript = 1
    };

    class decompiler_t final
    {
    public:
    private:
        std::string decompress_script(std::uint64_t script, script_type type);
    };
}