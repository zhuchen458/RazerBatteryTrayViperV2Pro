#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "device.h"

enum {
    TRAY_MESSAGE = WM_APP + 1,
    BATTERY_MESSAGE,
    POLL_TIMER = 1,
    DEVICE_TIMER,
    MENU_REFRESH = 101,
    MENU_AUTOSTART,
    MENU_EXIT,
    POLL_MS = 30000
};

static const wchar_t app_name[] = L"Razer Battery Tray · Viper V2 Pro";
static const wchar_t run_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t run_value[] = L"RazerBatteryTrayViperV2Pro";
static HWND window;
static UINT taskbar_created;
static NOTIFYICONDATAW tray;
static bool tray_added;
static bool refreshing;
static Battery current = {NO_DEVICE, -1, 0};
static int last_percent = -1;
static SYSTEMTIME last_read;
static bool has_result;
static const uint8_t glyphs[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
    {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1},
    {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}
};

static void fill_rect(uint32_t *pixels, int x, int y, int w, int h, uint32_t color)
{
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col)
            pixels[16 * row + col] = color;
}

static void draw_digit(uint32_t *pixels, int x, int digit, int width)
{
    for (int y = 0; y < 5; ++y)
        for (int col = 0; col < 3; ++col)
            if (glyphs[digit][y] & (4 >> col))
                fill_rect(pixels, x + col * width, 2 + y * 2, width, 2, 0xfff4f8f5);
}

static HICON battery_icon(Battery battery)
{
    uint32_t pixels[16 * 16];
    fill_rect(pixels, 0, 0, 16, 16, 0xff666e79);
    fill_rect(pixels, 1, 1, 14, 14, 0xff161b22);
    int level = battery.percent;
    if (level < 0) {
        fill_rect(pixels, 2, 7, 5, 2, 0xffa9b2be);
        fill_rect(pixels, 9, 7, 5, 2, 0xffa9b2be);
    } else {
        if (level == 100) {
            draw_digit(pixels, 2, 1, 1);
            draw_digit(pixels, 6, 0, 1);
            draw_digit(pixels, 10, 0, 1);
        } else if (level >= 10) {
            draw_digit(pixels, 1, level / 10, 2);
            draw_digit(pixels, 9, level % 10, 2);
        } else {
            draw_digit(pixels, 5, level, 2);
        }
        uint32_t color = battery.charging == 1 ? 0xff68dbe8 :
                         level <= 15 ? 0xffff687a :
                         level <= 40 ? 0xffffd05b : 0xff43e080;
        fill_rect(pixels, 2, 13, 12, 1, 0xff464e58);
        fill_rect(pixels, 2, 13, (level * 12 + 99) / 100, 1, color);
    }

    /* A 32-bit top-down DIB keeps the pixel grid sharp at the base tray size. */
    BITMAPINFO dib = {
        .bmiHeader = {
            .biSize = sizeof(BITMAPINFOHEADER), .biWidth = 16, .biHeight = -16,
            .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB
        }
    };
    void *buffer = NULL;
    HBITMAP color = CreateDIBSection(NULL, &dib, DIB_RGB_COLORS, &buffer, NULL, 0);
    const BYTE mask_pixels[32] = {0};
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, mask_pixels);
    HICON icon = NULL;
    if (color && buffer && mask) {
        memcpy(buffer, pixels, sizeof(pixels));
        ICONINFO info = {.fIcon = TRUE, .hbmMask = mask, .hbmColor = color};
        icon = CreateIconIndirect(&info);
    }
    if (mask)
        DeleteObject(mask);
    if (color)
        DeleteObject(color);
    return icon;
}

static void refresh_tray(void)
{
    if (current.percent >= 0) {
        const wchar_t *link = current.product == WIRED_PID ? L"USB 有线" : L"2.4G 无线";
        const wchar_t *charging = current.charging == 1 ? L" · 充电中" : L"";
        swprintf(tray.szTip, ARRAYSIZE(tray.szTip), L"毒蝰 V2 Pro · %d%% · %ls%ls",
                 current.percent, link, charging);
    } else {
        const wchar_t *status = !has_result ? L"正在读取…" :
            current.percent == NO_DEVICE ? L"未连接" : L"设备无响应，移动鼠标后重试";
        if (last_percent >= 0)
            swprintf(tray.szTip, ARRAYSIZE(tray.szTip),
                     L"毒蝰 V2 Pro · %ls\n上次 %d%%（%02u:%02u）", status, last_percent,
                     (unsigned)last_read.wHour, (unsigned)last_read.wMinute);
        else
            swprintf(tray.szTip, ARRAYSIZE(tray.szTip), L"毒蝰 V2 Pro · %ls", status);
    }

    HICON icon = battery_icon(current);
    if (!icon)
        return;

    HICON previous = tray.hIcon;
    tray.hIcon = icon;
    if (tray_added && !Shell_NotifyIconW(NIM_MODIFY, &tray))
        tray_added = false;
    if (!tray_added)
        tray_added = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
    if (previous)
        DestroyIcon(previous);
}

static DWORD WINAPI read_worker(void *parameter)
{
    HWND target = parameter;
    Battery reading = read_battery();
    WPARAM connection = (WPARAM)((reading.product << 2) | (reading.charging + 1));
    PostMessageW(target, BATTERY_MESSAGE, connection, (LPARAM)reading.percent);
    return 0;
}

static void request_refresh(void)
{
    if (refreshing)
        return;

    HANDLE worker = CreateThread(NULL, 0, read_worker, window, 0, NULL);
    if (worker) {
        refreshing = true;
        CloseHandle(worker);
    }
}

static bool startup_command(wchar_t *command, DWORD capacity)
{
    DWORD length = GetModuleFileNameW(NULL, command + 1, capacity - 2);
    if (!length || length >= capacity - 2)
        return false;
    command[0] = L'"';
    command[length + 1] = L'"';
    command[length + 2] = L'\0';
    return true;
}

static bool starts_at_logon(void)
{
    wchar_t current_path[MAX_PATH + 3];
    wchar_t value[MAX_PATH + 3];
    DWORD bytes = sizeof(value);
    return startup_command(current_path, ARRAYSIZE(current_path)) &&
           RegGetValueW(HKEY_CURRENT_USER, run_key, run_value, RRF_RT_REG_SZ,
                        NULL, value, &bytes) == ERROR_SUCCESS &&
           _wcsicmp(value, current_path) == 0;
}

static void toggle_autostart(void)
{
    LSTATUS status;
    if (starts_at_logon()) {
        status = RegDeleteKeyValueW(HKEY_CURRENT_USER, run_key, run_value);
    } else {
        wchar_t command[MAX_PATH + 3];
        if (!startup_command(command, ARRAYSIZE(command))) {
            MessageBoxW(window, L"程序路径过长，请将 EXE 移到较短的固定路径。", app_name, MB_ICONWARNING);
            return;
        }
        status = RegSetKeyValueW(HKEY_CURRENT_USER, run_key, run_value, REG_SZ,
                                 command, (DWORD)((wcslen(command) + 1) * sizeof(wchar_t)));
    }

    if (status != ERROR_SUCCESS)
        MessageBoxW(window, L"无法修改当前用户的开机启动设置。", app_name, MB_ICONWARNING);
}

static void tray_menu(void)
{
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;

    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, tray.szTip);
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_REFRESH, L"立即刷新");
    AppendMenuW(menu, MF_STRING | (starts_at_logon() ? MF_CHECKED : MF_UNCHECKED),
                MENU_AUTOSTART, L"开机自动启动");
    AppendMenuW(menu, MF_STRING, MENU_EXIT, L"退出");

    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(window);
    UINT action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                 cursor.x, cursor.y, 0, window, NULL);
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (action) {
    case MENU_REFRESH:
        request_refresh();
        break;
    case MENU_AUTOSTART:
        toggle_autostart();
        break;
    case MENU_EXIT:
        DestroyWindow(window);
        break;
    }
}

static LRESULT CALLBACK handle_message(HWND handle, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == taskbar_created && taskbar_created != 0) {
        tray_added = false;
        refresh_tray();
        return 0;
    }

    switch (message) {
    case WM_DEVICECHANGE:
    case WM_POWERBROADCAST:
        SetTimer(handle, DEVICE_TIMER, 800, NULL);
        return TRUE;
    case WM_TIMER:
        if (wparam == DEVICE_TIMER)
            KillTimer(handle, DEVICE_TIMER);
        request_refresh();
        return 0;
    case BATTERY_MESSAGE:
        refreshing = false;
        current = (Battery){(int)lparam, (int)(wparam & 3) - 1, (int)(wparam >> 2)};
        has_result = true;
        if (current.percent >= 0) {
            last_percent = current.percent;
            GetLocalTime(&last_read);
        }
        refresh_tray();
        return 0;
    case TRAY_MESSAGE:
        if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU)
            tray_menu();
        else if (lparam == WM_LBUTTONUP)
            request_refresh();
        return 0;
    case WM_DESTROY:
        if (tray_added)
            Shell_NotifyIconW(NIM_DELETE, &tray);
        if (tray.hIcon)
            DestroyIcon(tray.hIcon);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(handle, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    (void)previous;
    (void)command;
    (void)show;
    HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\RazerBatteryTrayViperV2Pro");
    if (!mutex)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    SetProcessDPIAware();
    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    const WNDCLASSW class = {
        .lpfnWndProc = handle_message,
        .hInstance = instance,
        .lpszClassName = L"RazerBatteryTrayViperV2Pro"
    };
    if (RegisterClassW(&class))
        window = CreateWindowExW(0, class.lpszClassName, app_name, WS_OVERLAPPED,
                                 0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (!window) {
        CloseHandle(mutex);
        return 1;
    }

    tray = (NOTIFYICONDATAW){
        .cbSize = sizeof(tray), .hWnd = window, .uID = 1,
        .uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP, .uCallbackMessage = TRAY_MESSAGE
    };
    refresh_tray();
    request_refresh();
    SetTimer(window, POLL_TIMER, POLL_MS, NULL);

    MSG message;
    BOOL result;
    while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CloseHandle(mutex);
    return result < 0 ? 1 : 0;
}
