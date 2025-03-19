#pragma once

#include <stdint.h>

enum BtnState {
    BTN_PRESSED = 1,
    BTN_HOLD,
    BTN_RELEASED,
};

const uint16_t G2L_PREAMBLE = '2G'; // "G2" in network byte order

#pragma pack(push, 1)
typedef struct {
    uint16_t preamble;
    uint8_t btnState;
    uint16_t batVolt;   // mV
} payload_t;
#pragma pack(pop)