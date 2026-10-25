#pragma once
#include <string>

namespace nexec_bridge {

void set_pid(unsigned long pid);
bool available();
bool execute(const std::string& source, std::string& error, const char* pipe_name = nullptr);

}
