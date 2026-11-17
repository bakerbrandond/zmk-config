/*
 * Dongle clock model: the computer sends the time over Raw HID and the dongle
 * shows it until the computer goes quiet. Pure functions, host-tested in
 * tests/clock.
 *
 * Packet (qmk-hid-host's TIME message): [0xAA, hour 0-23, minute 0-59], padded
 * to the report size. The host sends one whenever the minute changes, so the
 * time received is taken as the start of that minute and counted on from
 * there until the next packet. With no packet for longer than the timeout the
 * clock is hidden.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CLOCK_PACKET_TIME 0xAA
#define CLOCK_HIDDEN (-1)

struct clock_state {
    bool valid;        // a time has been received
    uint16_t base_min; // minute of the day it said (0..1439)
    int64_t base_ms;   // uptime when it arrived
};

static inline void clock_init(struct clock_state *c) {
    c->valid = false;
    c->base_min = 0;
    c->base_ms = 0;
}

// True for a well-formed time packet; fills hour and minute
static inline bool clock_parse(const uint8_t *data, size_t len, uint8_t *hour, uint8_t *minute) {
    if (data == NULL || len < 3 || data[0] != CLOCK_PACKET_TIME || data[1] > 23 || data[2] > 59) {
        return false;
    }
    *hour = data[1];
    *minute = data[2];
    return true;
}

static inline void clock_set(struct clock_state *c, uint8_t hour, uint8_t minute, int64_t now_ms) {
    c->valid = true;
    c->base_min = (uint16_t)(hour * 60 + minute);
    c->base_ms = now_ms;
}

// Minute of the day to show, or CLOCK_HIDDEN
static inline int clock_minutes(const struct clock_state *c, int64_t now_ms, int64_t timeout_ms) {
    if (!c->valid) {
        return CLOCK_HIDDEN;
    }
    int64_t elapsed = now_ms - c->base_ms;
    if (elapsed < 0 || elapsed > timeout_ms) {
        return CLOCK_HIDDEN;
    }
    return (int)((c->base_min + elapsed / 60000) % (24 * 60));
}

// "HH:MM" into buf (6 bytes)
static inline void clock_format(int minutes, char buf[6]) {
    int h = minutes / 60, m = minutes % 60;
    buf[0] = (char)('0' + h / 10);
    buf[1] = (char)('0' + h % 10);
    buf[2] = ':';
    buf[3] = (char)('0' + m / 10);
    buf[4] = (char)('0' + m % 10);
    buf[5] = '\0';
}
