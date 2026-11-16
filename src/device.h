#ifndef RAZER_DEVICE_H
#define RAZER_DEVICE_H

enum {
    NO_DEVICE = -1,
    NO_RESPONSE = -2,
    WIRED_PID = 0x00A5,
    WIRELESS_PID = 0x00A6
};

typedef struct {
    int percent;
    int charging;  /* -1 when the optional query did not return a valid reply */
    int product;
} Battery;

Battery read_battery(void);

#endif
