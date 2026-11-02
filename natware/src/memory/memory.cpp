#include "memory.h"
#include <iostream>

std::uint32_t memory_t::find_process_id(const std::string& process_name)
{
    std::uint32_t local_process_id = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return local_process_id;
    }

    PROCESSENTRY32W process_entry{};
    process_entry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &process_entry))
    {
        std::wstring w_process_name(process_name.begin(), process_name.end());
        do
        {
            if (!lstrcmpiW(w_process_name.c_str(), process_entry.szExeFile))
            {
                local_process_id = process_entry.th32ProcessID;
                process_id = local_process_id;
                break;
            }
        } while (Process32NextW(snapshot, &process_entry));
    }

    CloseHandle(snapshot);
    return local_process_id;
}

std::uint64_t memory_t::find_module_address(const std::string& module_name)
{
    std::uint64_t module_address = 0;

    if (!process_handle)
    {
        return module_address;
    }

    DWORD pID = GetProcessId(process_handle);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pID);

    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return module_address;
    }

    MODULEENTRY32 module_entry{};
    module_entry.dwSize = sizeof(MODULEENTRY32);

    if (Module32First(snapshot, &module_entry))
    {
        do
        {
            if (!_stricmp(module_name.c_str(), module_entry.szModule))
            {
                module_address = reinterpret_cast<uint64_t>(module_entry.modBaseAddr);
                base_address = module_address;
                module_size_ = module_entry.modBaseSize;
                break;
            }
        } while (Module32Next(snapshot, &module_entry));
    }

    CloseHandle(snapshot);
    return module_address;
}

void memory_t::detach()
{
    if (process_handle && process_handle != INVALID_HANDLE_VALUE)
        CloseHandle(process_handle);
    process_handle = nullptr;
    process_id = 0;
    base_address = 0;
    module_size_ = 0;
}

bool memory_t::attach_to_process(const std::string& process_name)
{
    std::uint32_t pID = find_process_id(process_name);
    if (pID == 0) return false;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD, false, pID);
    if (!process || process == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    process_handle = process;
    return true;
}

std::string memory_t::read_string(std::uint64_t address)
{
    std::int32_t string_length = read<std::int32_t>(address + 0x10);
    std::uint64_t string_address = (string_length >= 16) ? read<std::uint64_t>(address) : address;

    if (string_length == 0 || string_length > 255)
    {
        return "Unknown";
    }

    std::vector<char> buffer(string_length + 1, 0);
    Luck_ReadVirtualMemory(process_handle, reinterpret_cast<void*>(string_address), buffer.data(), static_cast<ULONG>(buffer.size()), nullptr);

    return std::string(buffer.data(), string_length);
}

void memory_t::write_string(std::uint64_t address, const std::string& value)
{
    if (value.empty())
    {
        return;
    }

    std::int32_t current_length = read<std::int32_t>(address + 0x10);
    std::int32_t new_length = static_cast<std::int32_t>(value.length());

    if (new_length >= 16)
    {
        if (current_length < 16)
        {
            return;
        }

        std::uint64_t heap_ptr = read<std::uint64_t>(address);
        if (heap_ptr == 0 || heap_ptr > 0x7FFFFFFFFFFF) return;

        std::int32_t capacity = read<std::int32_t>(address + 0x18);
        if (new_length > capacity) return;

        write<std::int32_t>(address + 0x10, new_length);
        write_buffer(heap_ptr, value.c_str(), new_length);
    }
    else
    {
        write<std::int32_t>(address + 0x10, new_length);
        write_buffer(address, value.c_str(), new_length);
    }
}

void memory_t::write_buffer(std::uint64_t address, const void* buffer, std::size_t size)
{
    Luck_WriteVirtualMemory(process_handle, reinterpret_cast<void*>(address), const_cast<void*>(buffer), static_cast<ULONG>(size), nullptr);
}

std::uint32_t memory_t::get_process_id()
{
    return process_id;
}

std::uint64_t memory_t::get_module_address()
{
    return base_address;
}

std::size_t memory_t::get_module_size()
{
    return module_size_;
}

HANDLE memory_t::get_process_handle()
{
    return process_handle;
}
