// Host tests for the strict-output decisions (src/strict_output_core.h)
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "strict_output_core.h"

enum { USB = 0, BLE = 1 };

static void sends_only_to_the_selected_output(void) {
    assert(strict_output_allows(BLE, BLE));
    assert(strict_output_allows(USB, USB));
    // Bluetooth selected but its host dropped: ZMK falls back to USB; block it
    assert(!strict_output_allows(USB, BLE));
    // USB selected but not ready: ZMK falls back to Bluetooth; block that too
    assert(!strict_output_allows(BLE, USB));
}

static void toggle_flips_the_selection(void) {
    assert(strict_output_toggled(USB, USB, BLE) == BLE);
    assert(strict_output_toggled(BLE, USB, BLE) == USB);
}

static void saved_selection_is_read_from_settings_bytes(void) {
    int out = -1;
    // ZMK builds with packed enums: one byte on the dongle
    unsigned char one = BLE;
    assert(strict_output_decode(&one, 1, 1, &out) && out == BLE);
    one = USB;
    assert(strict_output_decode(&one, 1, 1, &out) && out == USB);
    // an int-sized enum (little-endian) works too
    unsigned char four[4] = {BLE, 0, 0, 0};
    assert(strict_output_decode(four, 4, 4, &out) && out == BLE);
    // wrong size or unknown value: keep what we had
    out = BLE;
    one = USB;
    assert(!strict_output_decode(&one, 1, 4, &out) && out == BLE);
    one = 7;
    assert(!strict_output_decode(&one, 1, 1, &out) && out == BLE);
    unsigned char big[4] = {0, 1, 0, 0};
    assert(!strict_output_decode(big, 4, 4, &out) && out == BLE);
}

int main(void) {
    sends_only_to_the_selected_output();
    toggle_flips_the_selection();
    saved_selection_is_read_from_settings_bytes();
    puts("strict output: 3 tests passed");
    return 0;
}
