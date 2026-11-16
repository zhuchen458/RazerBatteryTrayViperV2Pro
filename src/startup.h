#ifndef RAZER_STARTUP_H
#define RAZER_STARTUP_H

#include <windows.h>
#include <stdbool.h>

int run_tray(HINSTANCE instance, bool background);
void format_message(wchar_t *buffer, DWORD capacity, const wchar_t *format, DWORD_PTR *args);
void startup_log(const char *stage, DWORD error);

#endif
