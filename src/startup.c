#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string.h>
#include <wchar.h>
#include "startup.h"

static wchar_t log_path[MAX_PATH];

void format_message(wchar_t *buffer, DWORD capacity, const wchar_t *format, DWORD_PTR *args)
{
    if (!FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                        format, 0, 0, buffer, capacity, (va_list *)args))
        buffer[0] = L'\0';
}

static void reset_startup_log(void)
{
    const wchar_t name[] = L"RazerBatteryTrayViperV2Pro-v1.0.3-startup.log";
    DWORD length = GetTempPathW(ARRAYSIZE(log_path), log_path);
    if (!length || length > ARRAYSIZE(log_path) - ARRAYSIZE(name)) {
        log_path[0] = L'\0';
        return;
    }
    memcpy(log_path + length, name, sizeof(name));
    HANDLE log = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log != INVALID_HANDLE_VALUE)
        CloseHandle(log);
}

void startup_log(const char *stage, DWORD error)
{
    if (!log_path[0])
        return;
    HANDLE log = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log == INVALID_HANDLE_VALUE)
        return;

    char line[128];
    DWORD_PTR args[] = {(DWORD_PTR)stage, (DWORD_PTR)error};
    DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                                  "%1!s! (Windows error %2!u!)%n", 0, 0, line,
                                  sizeof(line), (va_list *)args);
    DWORD written;
    if (length)
        WriteFile(log, line, length, &written, NULL);
    CloseHandle(log);
}

void WINAPI razor_start(void)
{
    reset_startup_log();
    startup_log("Razer Battery Tray v1.0.3 entry point reached", 0);

    int count = 0;
    wchar_t **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool background = arguments && count == 2 && lstrcmpW(arguments[1], L"--background") == 0;
    if (arguments)
        LocalFree(arguments);

    ExitProcess((UINT)run_tray(GetModuleHandleW(NULL), background));
}
