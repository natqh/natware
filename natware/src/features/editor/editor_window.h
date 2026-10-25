#pragma once
#include <Windows.h>

namespace natware_editor {
bool create();
void destroy();
void pump();
bool is_alive();
void trigger_attach();
}
