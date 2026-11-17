/* Host tests for the dongle clock model. Run: make -C tests/clock test */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "clock_core.h"

#define MIN_MS 60000
#define TIMEOUT_MS (3 * MIN_MS)

static void test_parse_time_packet(void) {
    uint8_t pkt[32] = {0xAA, 14, 32};
    uint8_t h, m;
    assert(clock_parse(pkt, sizeof(pkt), &h, &m));
    assert(h == 14 && m == 32);
}

/* Linux over Bluetooth (BlueZ HID-over-GATT) passes hidraw's report-ID byte
   through, so the dongle receives [0x00, 0xAA, hour, minute, ...] */
static void test_parse_skips_leading_report_id(void) {
    uint8_t pkt[33] = {0x00, 0xAA, 12, 34};
    uint8_t h, m;
    assert(clock_parse(pkt, sizeof(pkt), &h, &m));
    assert(h == 12 && m == 34);
    uint8_t shrt[3] = {0x00, 0xAA, 12};
    assert(!clock_parse(shrt, sizeof(shrt), &h, &m));
}

static void test_parse_rejects_other_types(void) {
    uint8_t vol[32] = {0xAB, 50};
    uint8_t h, m;
    assert(!clock_parse(vol, sizeof(vol), &h, &m));
}

static void test_parse_rejects_short_and_out_of_range(void) {
    uint8_t h, m;
    uint8_t shrt[2] = {0xAA, 14};
    assert(!clock_parse(shrt, sizeof(shrt), &h, &m));
    uint8_t bad_h[3] = {0xAA, 24, 0};
    assert(!clock_parse(bad_h, sizeof(bad_h), &h, &m));
    uint8_t bad_m[3] = {0xAA, 0, 60};
    assert(!clock_parse(bad_m, sizeof(bad_m), &h, &m));
    assert(!clock_parse(NULL, 0, &h, &m));
}

static void test_hidden_until_first_packet(void) {
    struct clock_state c;
    clock_init(&c);
    assert(clock_minutes(&c, 0, TIMEOUT_MS) == CLOCK_HIDDEN);
    assert(clock_minutes(&c, 5 * MIN_MS, TIMEOUT_MS) == CLOCK_HIDDEN);
}

static void test_shows_received_time(void) {
    struct clock_state c;
    clock_init(&c);
    clock_set(&c, 14, 32, 1000);
    assert(clock_minutes(&c, 1000, TIMEOUT_MS) == 14 * 60 + 32);
    assert(clock_minutes(&c, 1000 + MIN_MS - 1, TIMEOUT_MS) == 14 * 60 + 32);
}

static void test_keeps_counting_between_packets(void) {
    struct clock_state c;
    clock_init(&c);
    clock_set(&c, 14, 32, 0);
    assert(clock_minutes(&c, MIN_MS, TIMEOUT_MS) == 14 * 60 + 33);
    assert(clock_minutes(&c, 2 * MIN_MS + 5, TIMEOUT_MS) == 14 * 60 + 34);
}

static void test_wraps_at_midnight(void) {
    struct clock_state c;
    clock_init(&c);
    clock_set(&c, 23, 59, 0);
    assert(clock_minutes(&c, MIN_MS, TIMEOUT_MS) == 0);
}

static void test_hides_when_host_goes_quiet(void) {
    struct clock_state c;
    clock_init(&c);
    clock_set(&c, 9, 0, 0);
    assert(clock_minutes(&c, TIMEOUT_MS, TIMEOUT_MS) == 9 * 60 + 3);
    assert(clock_minutes(&c, TIMEOUT_MS + 1, TIMEOUT_MS) == CLOCK_HIDDEN);
}

static void test_new_packet_resets_timeout(void) {
    struct clock_state c;
    clock_init(&c);
    clock_set(&c, 9, 0, 0);
    clock_set(&c, 9, 3, TIMEOUT_MS);
    assert(clock_minutes(&c, TIMEOUT_MS + 2 * MIN_MS, TIMEOUT_MS) == 9 * 60 + 5);
}

static void test_format(void) {
    char buf[6];
    clock_format(7 * 60 + 5, buf);
    assert(buf[0] == '0' && buf[1] == '7' && buf[2] == ':' && buf[3] == '0' && buf[4] == '5' &&
           buf[5] == '\0');
    clock_format(23 * 60 + 59, buf);
    assert(buf[0] == '2' && buf[1] == '3' && buf[3] == '5' && buf[4] == '9');
}

static void expect_12h(int minutes, const char *want) {
    char buf[CLOCK_TEXT_MAX];
    clock_format_12h(minutes, buf);
    if (strcmp(buf, want) != 0) {
        printf("12h %d: got \"%s\", want \"%s\"\n", minutes, buf, want);
        assert(0);
    }
}

static void test_format_12h(void) {
    expect_12h(0, "12:00 AM");
    expect_12h(5, "12:05 AM");
    expect_12h(9 * 60 + 30, "9:30 AM");
    expect_12h(11 * 60 + 59, "11:59 AM");
    expect_12h(12 * 60, "12:00 PM");
    expect_12h(13 * 60 + 7, "1:07 PM");
    expect_12h(23 * 60 + 59, "11:59 PM");
}

int main(void) {
    test_parse_time_packet();
    test_parse_skips_leading_report_id();
    test_parse_rejects_other_types();
    test_parse_rejects_short_and_out_of_range();
    test_hidden_until_first_packet();
    test_shows_received_time();
    test_keeps_counting_between_packets();
    test_wraps_at_midnight();
    test_hides_when_host_goes_quiet();
    test_new_packet_resets_timeout();
    test_format();
    test_format_12h();
    printf("clock: 12 tests passed\n");
    return 0;
}
