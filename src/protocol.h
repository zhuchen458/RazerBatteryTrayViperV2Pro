#ifndef RAZER_PROTOCOL_H
#define RAZER_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum {
    REPORT_LENGTH = 91,
    TRANSACTION = 0x1F,
    BATTERY_COMMAND = 0x80,
    CHARGING_COMMAND = 0x84
};

/* Byte zero is the Windows HID report ID. The Razer payload starts at byte one. */
static uint8_t report_checksum(const uint8_t *report)
{
    uint8_t checksum = 0;
    for (int i = 3; i < 89; ++i)
        checksum ^= report[i];
    return checksum;
}

static void make_request(uint8_t *report, uint8_t command)
{
    memset(report, 0, REPORT_LENGTH);
    report[2] = TRANSACTION;
    report[6] = 2;
    report[7] = 0x07;
    report[8] = command;
    report[89] = report_checksum(report);
}

static bool read_response(const uint8_t *report, uint8_t command, uint8_t *value)
{
    /* Match the result status, packet count, protocol, command and checksum.
       Some Razer firmware returns a different transaction byte in the reply. */
    if (report[0] != 0 || report[1] != 2 || report[3] != 0 ||
        report[4] != 0 || report[5] != 0 || report[6] < 2 ||
        report[6] > 80 || report[7] != 0x07 || report[8] != command ||
        report[89] != report_checksum(report))
        return false;

    *value = report[10];
    return true;
}

static int battery_percent(uint8_t raw)
{
    return (raw * 100 + 127) / 255;
}

#endif
