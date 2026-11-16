#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "device.h"
#include "startup.h"

enum {
    TRAY_MESSAGE = WM_APP + 1,
    BATTERY_MESSAGE,
    SHOW_STATUS_MESSAGE,
    POLL_TIMER = 1,
    DEVICE_TIMER,
    MENU_REFRESH = 101,
    MENU_AUTOSTART,
    MENU_EXIT,
    MENU_STATUS,
    BUTTON_CLOSE,
    CHECK_AUTOSTART,
    POLL_MS = 30000
};

static const wchar_t app_name[] = L"毒蝰 V2 Pro 电量托盘 v1.0.4";
static const wchar_t window_class[] = L"RazerBatteryTrayViperV2Pro";
static const wchar_t run_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t run_value[] = L"RazerBatteryTrayViperV2Pro";
static const wchar_t background_option[] = L" --background";
enum { STARTUP_COMMAND_CAPACITY = MAX_PATH + 3 + ARRAYSIZE(background_option) };
static HWND window;
static HWND status_text;
static HWND tray_text;
static HWND autostart_checkbox;
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
        DWORD_PTR args[] = {(DWORD_PTR)current.percent, (DWORD_PTR)link, (DWORD_PTR)charging};
        format_message(tray.szTip, ARRAYSIZE(tray.szTip), L"毒蝰 V2 Pro · %1!d!%% · %2%3", args);
    } else {
        const wchar_t *status = !has_result ? L"正在读取…" :
            current.percent == NO_DEVICE ? L"未连接" : L"设备无响应，移动鼠标后重试";
        DWORD_PTR args[] = {(DWORD_PTR)status, (DWORD_PTR)last_percent,
                            (DWORD_PTR)last_read.wHour, (DWORD_PTR)last_read.wMinute};
        format_message(tray.szTip, ARRAYSIZE(tray.szTip), last_percent >= 0
            ? L"毒蝰 V2 Pro · %1%n上次 %2!d!%%（%3!02u!:%4!02u!）"
            : L"毒蝰 V2 Pro · %1", args);
    }

    HICON previous = tray.hIcon;
    HICON icon = battery_icon(current);
    if (icon)
        tray.hIcon = icon;
    if (tray_added && !Shell_NotifyIconW(NIM_MODIFY, &tray))
        tray_added = false;
    if (!tray_added && tray.hIcon)
        tray_added = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
    if (icon && previous)
        DestroyIcon(previous);

    SetWindowTextW(status_text, tray.szTip);
    SetWindowTextW(tray_text, tray_added
        ? L"托盘图标已就绪。可在任务栏右下角或 ^ 菜单找到。"
        : L"托盘图标暂未显示，程序会继续尝试添加。");
}

static HWND create_control(HINSTANCE instance, const wchar_t *class,
                           const wchar_t *label, DWORD style, int id,
                           int x, int y, int width, int height)
{
    HWND control = CreateWindowExW(0, class, label, WS_CHILD | WS_VISIBLE | style,
                                   x, y, width, height, window, (HMENU)(INT_PTR)id,
                                   instance, NULL);
    if (control)
        SendMessageW(control, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
    return control;
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
    } else {
        current = (Battery){NO_RESPONSE, -1, 0};
        has_result = true;
        refresh_tray();
        SetWindowTextW(status_text, L"无法启动电量读取，请点击「立即刷新」重试。");
    }
}

static bool startup_command(wchar_t *command, DWORD capacity, bool background)
{
    DWORD reserved = 2 + (background ? ARRAYSIZE(background_option) - 1 : 0);
    DWORD length = GetModuleFileNameW(NULL, command + 1, capacity - reserved);
    if (!length || length >= capacity - reserved)
        return false;
    command[0] = L'"';
    command[length + 1] = L'"';
    command[length + 2] = L'\0';
    if (background)
        memcpy(command + length + 2, background_option, sizeof(background_option));
    return true;
}

static bool starts_at_logon(void)
{
    wchar_t current_path[STARTUP_COMMAND_CAPACITY];
    wchar_t value[STARTUP_COMMAND_CAPACITY];
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, run_key, run_value, RRF_RT_REG_SZ,
                     NULL, value, &bytes) != ERROR_SUCCESS)
        return false;
    return (startup_command(current_path, ARRAYSIZE(current_path), true) &&
            lstrcmpiW(value, current_path) == 0) ||
           (startup_command(current_path, ARRAYSIZE(current_path), false) &&
            lstrcmpiW(value, current_path) == 0);
}

static void update_autostart_checkbox(void)
{
    SendMessageW(autostart_checkbox, BM_SETCHECK,
                 starts_at_logon() ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void show_status(void)
{
    update_autostart_checkbox();
    ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(window);
}

static void toggle_autostart(void)
{
    LSTATUS status;
    if (starts_at_logon()) {
        status = RegDeleteKeyValueW(HKEY_CURRENT_USER, run_key, run_value);
    } else {
        wchar_t command[STARTUP_COMMAND_CAPACITY];
        if (!startup_command(command, ARRAYSIZE(command), true)) {
            MessageBoxW(window, L"程序路径过长，请将 EXE 移到较短的固定路径。", app_name, MB_ICONWARNING);
            return;
        }
        status = RegSetKeyValueW(HKEY_CURRENT_USER, run_key, run_value, REG_SZ,
                                 command, (DWORD)((lstrlenW(command) + 1) * sizeof(wchar_t)));
    }

    update_autostart_checkbox();
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
    AppendMenuW(menu, MF_STRING, MENU_STATUS, L"打开电量窗口");
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
    case MENU_STATUS:
        show_status();
        break;
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
        if (!tray_added)
            refresh_tray();
        request_refresh();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == CHECK_AUTOSTART && HIWORD(wparam) == BN_CLICKED) {
            toggle_autostart();
            return 0;
        }
        if (LOWORD(wparam) == MENU_REFRESH) {
            request_refresh();
            return 0;
        }
        if (LOWORD(wparam) == BUTTON_CLOSE) {
            SendMessageW(handle, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_CLOSE:
        ShowWindow(handle, tray_added ? SW_HIDE : SW_MINIMIZE);
        return 0;
    case SHOW_STATUS_MESSAGE:
        show_status();
        return TRUE;
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
            show_status();
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

static void show_startup_error(const wchar_t *step, DWORD error)
{
    wchar_t message[160];
    DWORD_PTR args[] = {(DWORD_PTR)step, (DWORD_PTR)error};
    format_message(message, ARRAYSIZE(message), L"%1失败（Windows 错误 %2!u!）。", args);
    MessageBoxW(NULL, message, app_name, MB_OK | MB_ICONERROR);
}

int run_tray(HINSTANCE instance, bool background)
{
    HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\RazerBatteryTrayViperV2Pro");
    DWORD mutex_error = GetLastError();
    if (!mutex) {
        startup_log("Mutex creation failed", mutex_error);
        show_startup_error(L"启动程序", mutex_error);
        return 1;
    }
    if (mutex_error == ERROR_ALREADY_EXISTS) {
        startup_log("Another instance is running", 0);
        if (!background) {
            HWND running = FindWindowW(window_class, NULL);
            DWORD_PTR opened = 0;
            if (running) {
                DWORD process = 0;
                GetWindowThreadProcessId(running, &process);
                if (process)
                    AllowSetForegroundWindow(process);
                SendMessageTimeoutW(running, SHOW_STATUS_MESSAGE, 0, 0,
                                    SMTO_ABORTIFHUNG, 1000, &opened);
            }
            if (!opened)
                MessageBoxW(NULL, L"已有实例在运行。若旧版仍在后台，请先在任务管理器结束它，再打开新版。",
                            app_name, MB_OK | MB_ICONINFORMATION);
        }
        CloseHandle(mutex);
        return 0;
    }

    SetProcessDPIAware();
    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    const WNDCLASSW class = {
        .lpfnWndProc = handle_message,
        .hInstance = instance,
        .hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512)),
        .hbrBackground = (HBRUSH)(INT_PTR)(COLOR_WINDOW + 1),
        .lpszClassName = window_class
    };
    if (RegisterClassW(&class))
        window = CreateWindowExW(0, window_class, app_name,
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 470, 210,
                                 NULL, NULL, instance, NULL);
    if (!window) {
        DWORD error = GetLastError();
        startup_log("Window creation failed", error);
        show_startup_error(L"创建电量窗口", error);
        CloseHandle(mutex);
        return 1;
    }

    status_text = create_control(instance, L"STATIC", L"正在读取…", SS_LEFT,
                                 0, 22, 22, 416, 46);
    tray_text = create_control(instance, L"STATIC", L"正在添加托盘图标…", SS_LEFT,
                               0, 22, 76, 416, 28);
    autostart_checkbox = create_control(instance, L"BUTTON", L"登录时自动启动",
                                        BS_CHECKBOX | WS_TABSTOP, CHECK_AUTOSTART,
                                        268, 119, 160, 30);
    if (!status_text || !tray_text || !autostart_checkbox ||
        !create_control(instance, L"BUTTON", L"立即刷新", BS_PUSHBUTTON | WS_TABSTOP,
                        MENU_REFRESH, 22, 119, 108, 30) ||
        !create_control(instance, L"BUTTON", L"关闭窗口", BS_PUSHBUTTON | WS_TABSTOP,
                        BUTTON_CLOSE, 145, 119, 108, 30)) {
        DWORD error = GetLastError();
        startup_log("Window controls failed", error);
        show_startup_error(L"初始化电量窗口", error);
        DestroyWindow(window);
        CloseHandle(mutex);
        return 1;
    }

    tray = (NOTIFYICONDATAW){
        .cbSize = sizeof(tray), .hWnd = window, .uID = 1,
        .uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP, .uCallbackMessage = TRAY_MESSAGE
    };
    if (!background) {
        show_status();
        UpdateWindow(window);
        startup_log("Status window shown", 0);
    } else {
        startup_log("Started in background mode", 0);
    }
    refresh_tray();
    startup_log(tray_added ? "Notification icon added" : "Notification icon unavailable", 0);
    request_refresh();
    if (!SetTimer(window, POLL_TIMER, POLL_MS, NULL)) {
        DWORD error = GetLastError();
        startup_log("Poll timer failed", error);
        show_startup_error(L"启动定时刷新", error);
    }

    MSG message;
    BOOL result;
    while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (result < 0) {
        DWORD error = GetLastError();
        startup_log("Message loop failed", error);
        show_startup_error(L"读取窗口消息", error);
        DestroyWindow(window);
    }
    CloseHandle(mutex);
    return result < 0 ? 1 : 0;
}
