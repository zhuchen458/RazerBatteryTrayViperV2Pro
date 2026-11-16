#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
#include "device.h"
#include "protocol.h"

#define VENDOR_ID 0x1532
#define SHARE_FLAGS (FILE_SHARE_READ | FILE_SHARE_WRITE)

static bool path_contains(const wchar_t *path, const wchar_t *part)
{
    for (; *path; ++path) {
        size_t offset = 0;
        while (part[offset] && path[offset] == part[offset])
            ++offset;
        if (!part[offset])
            return true;
    }
    return false;
}

static bool query(HANDLE device, DWORD length, uint8_t command, uint8_t *value)
{
    HANDLE heap = GetProcessHeap();
    uint8_t *report = HeapAlloc(heap, 0, length);
    if (!report)
        return false;

    bool success = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        memset(report, 0, length);
        make_request(report, command);
        if (!HidD_SetFeature(device, report, length))
            break;

        Sleep(32);
        memset(report, 0, length);
        if (HidD_GetFeature(device, report, length) &&
            read_response(report, command, value)) {
            success = true;
            break;
        }
        Sleep(10);
    }
    HeapFree(heap, 0, report);
    return success;
}

static void try_collection(const wchar_t *path, int product, bool prefer_mouse,
                           Battery *battery)
{
    /* Razer's control collection can be shared with Windows' mouse driver.
       A zero-access handle still permits HID feature requests on that collection. */
    HANDLE device = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, SHARE_FLAGS,
                                NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE)
        device = CreateFileW(path, 0, SHARE_FLAGS, NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE)
        return;

    HIDD_ATTRIBUTES attributes = {.Size = sizeof(attributes)};
    PHIDP_PREPARSED_DATA data = NULL;
    HIDP_CAPS caps = {0};
    if (HidD_GetAttributes(device, &attributes) &&
        attributes.VendorID == VENDOR_ID && attributes.ProductID == product &&
        HidD_GetPreparsedData(device, &data)) {
        NTSTATUS status = HidP_GetCaps(data, &caps);
        HidD_FreePreparsedData(data);
        if (status == HIDP_STATUS_SUCCESS) {
            const bool mouse = caps.UsagePage == 0x01 && caps.Usage == 0x02;
            if (mouse == prefer_mouse) {
                DWORD length = caps.FeatureReportByteLength;
                if (length < REPORT_LENGTH)
                    length = REPORT_LENGTH;

                uint8_t value;
                if (query(device, length, BATTERY_COMMAND, &value)) {
                    battery->percent = battery_percent(value);
                    battery->product = product;
                    if (query(device, length, CHARGING_COMMAND, &value) && value <= 1)
                        battery->charging = value;
                }
            }
        }
    }
    CloseHandle(device);
}

Battery read_battery(void)
{
    Battery battery = {NO_DEVICE, -1, 0};
    GUID hid;
    HidD_GetHidGuid(&hid);
    HDEVINFO devices = SetupDiGetClassDevsW(&hid, NULL, NULL,
                                           DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE)
        return (Battery){NO_RESPONSE, -1, 0};

    const struct {
        const wchar_t *id;
        int product;
    } supported[] = {
        {L"vid_1532&pid_00a5", WIRED_PID},
        {L"vid_1532&pid_00a6", WIRELESS_PID}
    };

    for (size_t model = 0; model < ARRAYSIZE(supported) && battery.percent < 0; ++model) {
        for (int pass = 0; pass < 2 && battery.percent < 0; ++pass) {
            for (DWORD index = 0; battery.percent < 0; ++index) {
                SP_DEVICE_INTERFACE_DATA item = {.cbSize = sizeof(item)};
                if (!SetupDiEnumDeviceInterfaces(devices, NULL, &hid, index, &item))
                    break;

                DWORD bytes = 0;
                SetupDiGetDeviceInterfaceDetailW(devices, &item, NULL, 0, &bytes, NULL);
                if (bytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
                    continue;

                PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail = HeapAlloc(GetProcessHeap(), 0, bytes);
                if (!detail)
                    continue;

                detail->cbSize = sizeof(*detail);
                if (SetupDiGetDeviceInterfaceDetailW(devices, &item, detail, bytes, NULL, NULL)) {
                    wchar_t *path = detail->DevicePath;
                    CharLowerBuffW(path, (DWORD)lstrlenW(path));
                    if (path_contains(path, supported[model].id)) {
                        battery.percent = NO_RESPONSE;
                        /* Both Viper V2 Pro connections issue control commands on USB interface 0. */
                        if (path_contains(path, L"&mi_00") || !path_contains(path, L"&mi_"))
                            try_collection(path, supported[model].product, pass == 0, &battery);
                    }
                }
                HeapFree(GetProcessHeap(), 0, detail);
            }
        }
    }

    SetupDiDestroyDeviceInfoList(devices);
    return battery;
}
