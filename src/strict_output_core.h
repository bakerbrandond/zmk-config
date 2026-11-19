/*
 * Strict output decisions, free of ZMK headers so they can be tested on the
 * host (tests/strict_output). Outputs are ZMK's enum zmk_transport values as
 * ints: USB 0, Bluetooth 1.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

// A report may go out only on the output the user selected
static inline bool strict_output_allows(int sending, int selected) { return sending == selected; }

// The selection after ZMK's "toggle output" behavior
static inline int strict_output_toggled(int selected, int usb, int ble) {
    return selected == usb ? ble : usb;
}

// ZMK saves the selection as "endpoints/preferred": the raw enum, which is
// enum_size bytes (1 with ZMK's packed enums), little-endian. Returns false,
// leaving *out alone, if the bytes aren't a known value.
static inline bool strict_output_decode(const void *buf, size_t len, size_t enum_size, int *out) {
    if (len != enum_size || enum_size == 0 || enum_size > sizeof(unsigned long)) {
        return false;
    }
    const unsigned char *b = buf;
    unsigned long value = 0;
    for (size_t i = 0; i < len; i++) {
        value |= (unsigned long)b[i] << (8 * i);
    }
    if (value != 0 && value != 1) {
        return false;
    }
    *out = (int)value;
    return true;
}
