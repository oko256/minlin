#include "minlin/minlin.h"
#include "minlin/minlin_hw.h"
#include <string.h>
#include <unity.h>

// Global LIN bus IO data buffer for unit testing purposes
uint8_t g_io_data[32] = {0};
uint8_t g_io_data_len = 0;

// Critical section tracking from the HW test stubs
extern int  g_critical_depth;
extern bool g_critical_error;

// For testing with responder subscribe callback
minlin_frame g_responder_subscribe_expected;
bool         g_responder_subscribe_called = false;

// For simulating receive interrupt in the middle of user callbacks (i.e. during responder task).
// If set, callbacks simulate BREAK and feed these bytes to the responder.
static minlin_responder* s_inject_resp  = NULL;
static const uint8_t*    s_inject_bytes = NULL;
static size_t            s_inject_len   = 0;

static void s_clear_io_data(void)
{
    memset(g_io_data, 0, 32);
    g_io_data_len = 0;
}

// Simulates BREAK and receives the given bytes, expecting each byte to be handled without errors.
static void s_receive_frame(minlin_responder* resp, const uint8_t* bytes, size_t len)
{
    minlin_hw_transmit_break();
    for (size_t i = 0; i < len; ++i) {
        TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_receive_byte(resp, bytes[i]));
    }
}

static void s_inject_if_requested(void)
{
    // Callbacks must never be called while inside a critical section.
    TEST_ASSERT_EQUAL(0, g_critical_depth);
    if (s_inject_resp == NULL) {
        return;
    }
    minlin_responder* resp = s_inject_resp;
    s_inject_resp          = NULL;
    minlin_hw_transmit_break();
    for (size_t i = 0; i < s_inject_len; ++i) {
        minlin_responder_receive_byte(resp, s_inject_bytes[i]);
    }
}

void setUp(void)
{
    s_clear_io_data();
    minlin_hw_check_and_clear_break();
    g_critical_depth             = 0;
    g_critical_error             = false;
    g_responder_subscribe_called = false;
    memset(&g_responder_subscribe_expected, 0, sizeof(g_responder_subscribe_expected));
    s_inject_resp = NULL;
}

void tearDown(void)
{
    // Critical sections must always be balanced, never nested, and never held during transmit.
    TEST_ASSERT_EQUAL(0, g_critical_depth);
    TEST_ASSERT_FALSE(g_critical_error);
}

void test_id_to_pid(void)
{
    minlin_res r = MINLIN_RES_OK;
    TEST_ASSERT_EQUAL(0, minlin_id_to_pid(0x40, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_ID_OUT_OF_BOUNDS, r);
    TEST_ASSERT_EQUAL(0x25, minlin_id_to_pid(0x25, &r)); // parity 0b00
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0x73, minlin_id_to_pid(0x33, &r)); // parity 0b01
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0xB1, minlin_id_to_pid(0x31, &r)); // parity 0b10
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0xF0, minlin_id_to_pid(0x30, &r)); // parity 0b11
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0x25, minlin_id_to_pid(0x25, NULL)); // without result variable
}

void test_checksum(void)
{
    minlin_res    r     = MINLIN_RES_OK;
    minlin_config c     = {0};
    uint8_t       p[10] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA};

    // Test for null parameter errors on null inputs.
    TEST_ASSERT_EQUAL(0, minlin_checksum(NULL, 0x40, p, 4, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_NULL_PARAM, r);
    TEST_ASSERT_EQUAL(0, minlin_checksum(&c, 0x40, NULL, 4, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_NULL_PARAM, r);
    TEST_ASSERT_EQUAL(0, minlin_checksum(NULL, 0x40, NULL, 4, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_NULL_PARAM, r);

    // Test default config where enhanced checksum is enabled on all other than diagnostic frames.
    // Also tests out of bounds IDs and invalid payload sizes.
    TEST_ASSERT_EQUAL(0, minlin_checksum(&c, 0x40, p, 4, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_ID_OUT_OF_BOUNDS, r);
    TEST_ASSERT_EQUAL(0x5C, minlin_checksum(&c, 0x12, p, 1, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0xC2, minlin_checksum(&c, 0x12, p, 4, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0x07, minlin_checksum(&c, 0x12, p, 8, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0xA8, minlin_checksum(&c, 0x30, p, 8, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0, minlin_checksum(&c, 0x12, p, 9, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_INVALID_PAYLOAD_SIZE, r);
    // By default, diagnostic frames should still have classic checksums.
    TEST_ASSERT_EQUAL(0x99, minlin_checksum(&c, MINLIN_DIAG_REQUEST_ID, p, 8, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0x99, minlin_checksum(&c, MINLIN_DIAG_RESPONSE_ID, p, 8, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);

    // Test that if we specifically override diagnostic frames to behave as normal frames,
    // we get enhanced checksums on those too.
    c.diagnostic_frames_as_normal_frames = true;
    TEST_ASSERT_EQUAL(0x5D, minlin_checksum(&c, MINLIN_DIAG_REQUEST_ID, p, 8, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0x1C, minlin_checksum(&c, MINLIN_DIAG_RESPONSE_ID, p, 8, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);

    // Check that if we specifically override to always use just classic checksums,
    // then all checksums follow the classic way.
    c.classic_checksum = true;
    TEST_ASSERT_EQUAL(0xEE, minlin_checksum(&c, 0x12, p, 1, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0x55, minlin_checksum(&c, 0x12, p, 4, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0x99, minlin_checksum(&c, 0x12, p, 8, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0x99, minlin_checksum(&c, 0x30, p, 8, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0x99, minlin_checksum(&c, MINLIN_DIAG_REQUEST_ID, p, 8, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0x99, minlin_checksum(&c, MINLIN_DIAG_RESPONSE_ID, p, 8, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    // Changing this should not matter when using classic checksums everywhere anyway.
    c.diagnostic_frames_as_normal_frames = false;
    TEST_ASSERT_EQUAL(0x99, minlin_checksum(&c, MINLIN_DIAG_REQUEST_ID, p, 8, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
    TEST_ASSERT_EQUAL(0x99, minlin_checksum(&c, MINLIN_DIAG_RESPONSE_ID, p, 8, &r));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, r);
}

void test_commander(void)
{
    s_clear_io_data();
    minlin_hw_check_and_clear_break();

    minlin_schedule_entry sch[3] = {
        {.id = 0x11, .frame_slot_ms = 100}, {.id = 0x12, .frame_slot_ms = 50}, {.id = 0x06, .frame_slot_ms = 80}};
    minlin_commander com;
    minlin_commander_init(&com);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_set_schedule(&com, sch, MINLIN_ARRAY_LEN(sch), true));
    TEST_ASSERT_FALSE(com.stopped);

    // First task call should send the first scheduled entry header.
    uint32_t t = 0;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, t));
    TEST_ASSERT_EQUAL(2, g_io_data_len);
    uint8_t exp1[2] = {0x55, 0x11};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp1, g_io_data, MINLIN_ARRAY_LEN(exp1));

    s_clear_io_data();

    // 10 ms is not enough to expire the frame slot (100 ms), so this shouldn't do anything yet.
    t += 10;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, t));
    TEST_ASSERT_EQUAL(g_io_data_len, 0);
    // 10 + 90 ms = 100 ms, so the next entry header should transmit.
    t += 90;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, t));
    TEST_ASSERT_EQUAL(g_io_data_len, 2);
    uint8_t exp2[2] = {0x55, 0x92};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp2, g_io_data, MINLIN_ARRAY_LEN(exp2));

    s_clear_io_data();

    // 100 ms is for sure more than 50 ms slot time, so next header should transmit again.
    t += 100;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, t));
    TEST_ASSERT_EQUAL(g_io_data_len, 2);
    uint8_t exp3[2] = {0x55, 0x06};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp3, g_io_data, MINLIN_ARRAY_LEN(exp3));

    s_clear_io_data();

    // Make sure timer is cleared on each transmit; 70 ms should not expire frame slot yet.
    t += 70;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, t));
    TEST_ASSERT_EQUAL(g_io_data_len, 0);
    TEST_ASSERT_FALSE(com.stopped);
    // But now it should jump back to the first schedule entry.
    t += 10;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, t));
    TEST_ASSERT_EQUAL(2, g_io_data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp1, g_io_data, MINLIN_ARRAY_LEN(exp1));
    TEST_ASSERT_FALSE(com.stopped);

    // Now test the same thing without looping, should stop and not jump to first entry again.
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_set_schedule(&com, sch, MINLIN_ARRAY_LEN(sch), false));
    TEST_ASSERT_FALSE(com.stopped);
    s_clear_io_data();
    t += 1000;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, t));
    TEST_ASSERT_EQUAL(2, g_io_data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp1, g_io_data, MINLIN_ARRAY_LEN(exp1));
    s_clear_io_data();
    t += 100;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, t));
    TEST_ASSERT_EQUAL(g_io_data_len, 2);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp2, g_io_data, MINLIN_ARRAY_LEN(exp2));
    TEST_ASSERT_FALSE(com.stopped);
    s_clear_io_data();
    t += 100;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, t));
    // This was the last entry and this schedule is not looping; should be stopped now.
    TEST_ASSERT_TRUE(com.stopped);
    TEST_ASSERT_EQUAL(g_io_data_len, 2);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp3, g_io_data, MINLIN_ARRAY_LEN(exp3));
    s_clear_io_data();
    // Should not jump back to first entry since looping if disabled.
    t += 100;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, t));
    TEST_ASSERT_EQUAL(0, g_io_data_len);
    TEST_ASSERT_TRUE(com.stopped);
}

void test_responder(void)
{
    s_clear_io_data();
    minlin_hw_check_and_clear_break();

    minlin_frame frames[3] = {
        {.id = 0x11, .type = MINLIN_PUBLISH, .payload = {0}, .payload_size = 4},
        {.id = 0x12, .type = MINLIN_SUBSCRIBE, .payload = {0}, .payload_size = 2},
        {.id = 0x06, .type = MINLIN_PUBLISH, .payload = {0}, .payload_size = 8}};
    minlin_responder resp;
    minlin_responder_init(&resp);
    frames[1].payload_size = 9;
    TEST_ASSERT_EQUAL(
        MINLIN_RES_INVALID_PAYLOAD_SIZE, minlin_responder_set_frames(&resp, frames, MINLIN_ARRAY_LEN(frames)));
    frames[1].payload_size = 2;
    frames[2].id           = 0xFE;
    TEST_ASSERT_EQUAL(
        MINLIN_RES_ID_OUT_OF_BOUNDS, minlin_responder_set_frames(&resp, frames, MINLIN_ARRAY_LEN(frames)));
    frames[2].id = 0x06;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_set_frames(&resp, frames, MINLIN_ARRAY_LEN(frames)));

    uint8_t test_pid_11[2] = {
        0x55, // SYNC
        0x11 // PID
    };
    uint8_t test_pid_12[5] = {
        0x55, // SYNC
        0x92, // PID
        0x10, // Payload
        0x20, // -"-
        0x3D // Enhanced checksum
    };
    uint8_t test_pid_06[2] = {
        0x55, // SYNC
        0x06 // PID
    };

    // Nothing should happen before BREAK starts a new frame.
    g_responder_subscribe_expected.id = 0;
    g_responder_subscribe_called      = false;
    for (size_t i = 0; i < MINLIN_ARRAY_LEN(test_pid_11); ++i) {
        minlin_responder_task(&resp);
        TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_receive_byte(&resp, test_pid_11[i]));
        TEST_ASSERT_EQUAL(0, g_io_data_len);
    }
    for (size_t i = 0; i < MINLIN_ARRAY_LEN(test_pid_11); ++i) {
        minlin_responder_task(&resp);
        TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_receive_byte(&resp, test_pid_11[i]));
        TEST_ASSERT_EQUAL(0, g_io_data_len);
    }
    for (size_t i = 0; i < MINLIN_ARRAY_LEN(test_pid_11); ++i) {
        minlin_responder_task(&resp);
        TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_receive_byte(&resp, test_pid_11[i]));
        TEST_ASSERT_EQUAL(0, g_io_data_len);
    }
    minlin_responder_task(&resp);
    TEST_ASSERT_EQUAL(0, g_io_data_len);
    TEST_ASSERT_FALSE(g_responder_subscribe_called);

    // Send a simulated BREAK and then SYNC + PID 0x11 should trigger a publish.
    s_clear_io_data();
    minlin_hw_transmit_break();
    for (size_t i = 0; i < MINLIN_ARRAY_LEN(test_pid_11); ++i) {
        minlin_responder_task(&resp);
        TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_receive_byte(&resp, test_pid_11[i]));
        TEST_ASSERT_EQUAL(0, g_io_data_len);
    }
    minlin_responder_task(&resp);
    TEST_ASSERT_EQUAL(5, g_io_data_len);
    uint8_t exp1[5] = {0x01, 0x02, 0x03, 0x04, 0xE4};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp1, g_io_data, MINLIN_ARRAY_LEN(exp1));
    TEST_ASSERT_FALSE(g_responder_subscribe_called);

    // Send a simulated BREAK and then SYNC + PID 0x12 should trigger a subscribe.
    s_clear_io_data();
    g_responder_subscribe_expected.id           = 0x12;
    g_responder_subscribe_expected.type         = MINLIN_SUBSCRIBE;
    g_responder_subscribe_expected.payload_size = 2;
    g_responder_subscribe_expected.payload[0]   = 0x10;
    g_responder_subscribe_expected.payload[1]   = 0x20;
    minlin_hw_transmit_break();
    for (size_t i = 0; i < MINLIN_ARRAY_LEN(test_pid_12); ++i) {
        minlin_responder_task(&resp);
        TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_receive_byte(&resp, test_pid_12[i]));
    }
    minlin_responder_task(&resp);
    TEST_ASSERT_TRUE(g_responder_subscribe_called);
    g_responder_subscribe_called = false;

    // Again, nothing should happen before BREAK starts a new frame.
    g_responder_subscribe_expected.id = 0;
    g_responder_subscribe_called      = false;
    for (size_t i = 0; i < MINLIN_ARRAY_LEN(test_pid_11); ++i) {
        minlin_responder_task(&resp);
        TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_receive_byte(&resp, test_pid_11[i]));
        TEST_ASSERT_EQUAL(0, g_io_data_len);
    }
    for (size_t i = 0; i < MINLIN_ARRAY_LEN(test_pid_11); ++i) {
        minlin_responder_task(&resp);
        TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_receive_byte(&resp, test_pid_11[i]));
        TEST_ASSERT_EQUAL(0, g_io_data_len);
    }
    for (size_t i = 0; i < MINLIN_ARRAY_LEN(test_pid_11); ++i) {
        minlin_responder_task(&resp);
        TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_receive_byte(&resp, test_pid_11[i]));
        TEST_ASSERT_EQUAL(0, g_io_data_len);
    }
    minlin_responder_task(&resp);
    TEST_ASSERT_EQUAL(0, g_io_data_len);
    TEST_ASSERT_FALSE(g_responder_subscribe_called);

    // Send a simulated BREAK and then SYNC + PID 0x06 should trigger another publish.
    s_clear_io_data();
    minlin_hw_transmit_break();
    for (size_t i = 0; i < MINLIN_ARRAY_LEN(test_pid_06); ++i) {
        minlin_responder_task(&resp);
        TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_receive_byte(&resp, test_pid_06[i]));
        TEST_ASSERT_EQUAL(0, g_io_data_len);
    }
    minlin_responder_task(&resp);
    TEST_ASSERT_EQUAL(9, g_io_data_len);
    uint8_t exp2[9] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x55};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp2, g_io_data, MINLIN_ARRAY_LEN(exp2));
    TEST_ASSERT_FALSE(g_responder_subscribe_called);
}

void test_commander_set_schedule_validation(void)
{
    minlin_schedule_entry sch[2]      = {{.id = 0x11, .frame_slot_ms = 10}, {.id = 0x12, .frame_slot_ms = 10}};
    minlin_schedule_entry bad_id[1]   = {{.id = 0x40, .frame_slot_ms = 10}};
    minlin_schedule_entry bad_slot[1] = {{.id = 0x11, .frame_slot_ms = 0}};
    minlin_commander      com;
    minlin_commander_init(&com);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_set_schedule(&com, sch, MINLIN_ARRAY_LEN(sch), true));

    // Invalid schedules are rejected and the previous schedule is kept.
    TEST_ASSERT_EQUAL(MINLIN_RES_NULL_PARAM, minlin_commander_set_schedule(NULL, sch, 2, true));
    TEST_ASSERT_EQUAL(MINLIN_RES_NULL_PARAM, minlin_commander_set_schedule(&com, NULL, 2, true));
    TEST_ASSERT_EQUAL(MINLIN_RES_ID_OUT_OF_BOUNDS, minlin_commander_set_schedule(&com, bad_id, 1, true));
    TEST_ASSERT_EQUAL(MINLIN_RES_INVALID_SLOT_TIME, minlin_commander_set_schedule(&com, bad_slot, 1, true));
    TEST_ASSERT_EQUAL_PTR(sch, com.schedule_entries);
    TEST_ASSERT_EQUAL(2, com.schedule_entry_count);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, 0));
    uint8_t exp[2] = {0x55, 0x11};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, g_io_data, MINLIN_ARRAY_LEN(exp));

    // NULL schedule with zero entries disables scheduling.
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_set_schedule(&com, NULL, 0, true));
    s_clear_io_data();
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, 100));
    TEST_ASSERT_EQUAL(0, g_io_data_len);
}

void test_commander_restart_after_stop(void)
{
    // Regression: frame slot of a stopped schedule wasn't counted down, delaying the next schedule.
    minlin_schedule_entry sch[1] = {{.id = 0x11, .frame_slot_ms = 500}};
    minlin_commander      com;
    minlin_commander_init(&com);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_set_schedule(&com, sch, 1, false));
    uint32_t t = 0;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, t));
    TEST_ASSERT_TRUE(com.stopped);
    for (int i = 0; i < 2000; ++i) {
        TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, ++t));
    }
    s_clear_io_data();
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_set_schedule(&com, sch, 1, false));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, ++t));
    TEST_ASSERT_EQUAL(2, g_io_data_len);

    // Same when the schedule is disabled in the middle of a frame slot and the slot then expires.
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_set_schedule(&com, NULL, 0, false));
    t += 600;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, t));
    s_clear_io_data();
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_set_schedule(&com, sch, 1, false));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, ++t));
    TEST_ASSERT_EQUAL(2, g_io_data_len);
}

void test_commander_jitter_compensation(void)
{
    minlin_schedule_entry sch[2] = {{.id = 0x11, .frame_slot_ms = 10}, {.id = 0x12, .frame_slot_ms = 10}};
    minlin_commander      com;
    minlin_commander_init(&com);
    com.max_jitter_ms = 5;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_set_schedule(&com, sch, MINLIN_ARRAY_LEN(sch), true));

    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, 0));
    TEST_ASSERT_EQUAL(2, g_io_data_len);
    // 3 ms late is within jitter budget, so the next slot is shortened to keep the time base.
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, 13));
    TEST_ASSERT_EQUAL(4, g_io_data_len);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, 19));
    TEST_ASSERT_EQUAL(4, g_io_data_len);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, 20));
    TEST_ASSERT_EQUAL(6, g_io_data_len);
    // 8 ms late exceeds jitter budget, so the timing restarts from the late call.
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, 38));
    TEST_ASSERT_EQUAL(8, g_io_data_len);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, 47));
    TEST_ASSERT_EQUAL(8, g_io_data_len);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_commander_task(&com, 48));
    TEST_ASSERT_EQUAL(10, g_io_data_len);
}

void test_responder_set_frames_validation(void)
{
    minlin_frame     frames[1] = {{.id = 0x12, .type = MINLIN_SUBSCRIBE, .payload = {0}, .payload_size = 2}};
    minlin_responder resp;
    minlin_responder_init(&resp);
    TEST_ASSERT_EQUAL(MINLIN_RES_NULL_PARAM, minlin_responder_set_frames(NULL, frames, 1));
    // Regression: this used to dereference NULL.
    TEST_ASSERT_EQUAL(MINLIN_RES_NULL_PARAM, minlin_responder_set_frames(&resp, NULL, 3));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_set_frames(&resp, NULL, 0));
    frames[0].payload_size = 0;
    TEST_ASSERT_EQUAL(MINLIN_RES_INVALID_PAYLOAD_SIZE, minlin_responder_set_frames(&resp, frames, 1));
    TEST_ASSERT_NULL(resp.frames);
    frames[0].payload_size = 2;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_set_frames(&resp, frames, 1));
    TEST_ASSERT_EQUAL_PTR(frames, resp.frames);
}

void test_responder_set_frames_drops_partial_header(void)
{
    // Regression: a partially received header was kept, so if the application stopped feeding bytes
    // in the middle of it, the next BREAK after resuming was reported as unexpected.
    minlin_frame     frames[1] = {{.id = 0x12, .type = MINLIN_SUBSCRIBE, .payload = {0}, .payload_size = 2}};
    minlin_responder resp;
    minlin_responder_init(&resp);
    const uint8_t frame[5]   = {0x55, 0x92, 0x10, 0x20, 0x3D};
    const uint8_t partial[2] = {0x00, 0x55}; // BREAK reported separately, then SYNC

    for (size_t len = 1; len <= sizeof(partial); ++len) {
        TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_set_frames(&resp, frames, 1));
        s_receive_frame(&resp, partial, len);
        TEST_ASSERT_NOT_EQUAL(MINLIN_RESPONDER_IDLE, resp.state);

        TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_set_frames(&resp, frames, 1));
        TEST_ASSERT_EQUAL(MINLIN_RESPONDER_IDLE, resp.state);
        s_receive_frame(&resp, frame, sizeof(frame));
        g_responder_subscribe_called   = false;
        g_responder_subscribe_expected = (minlin_frame){
            .id = 0x12, .type = MINLIN_SUBSCRIBE, .payload = {0x10, 0x20}, .payload_size = 2};
        TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_task(&resp));
        TEST_ASSERT_TRUE(g_responder_subscribe_called);
    }

    // Also a partially received response is dropped.
    s_receive_frame(&resp, frame, 3);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_set_frames(&resp, frames, 1));
    TEST_ASSERT_EQUAL(MINLIN_RESPONDER_IDLE, resp.state);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_receive_byte(&resp, 0x20));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_receive_byte(&resp, 0x3D));
    TEST_ASSERT_EQUAL(MINLIN_RESPONDER_IDLE, resp.state);
}

void test_responder_bad_checksum_keeps_payload(void)
{
    minlin_frame     frames[1] = {{.id = 0x12, .type = MINLIN_SUBSCRIBE, .payload = {0xAA, 0xBB}, .payload_size = 2}};
    minlin_responder resp;
    minlin_responder_init(&resp);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_set_frames(&resp, frames, 1));

    const uint8_t header[4] = {0x55, 0x92, 0x10, 0x20};
    s_receive_frame(&resp, header, sizeof(header));
    TEST_ASSERT_EQUAL(MINLIN_RES_MALFORMED_CHECKSUM, minlin_responder_receive_byte(&resp, 0x3E));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_task(&resp));
    TEST_ASSERT_FALSE(g_responder_subscribe_called);
    TEST_ASSERT_EQUAL_HEX8(0xAA, frames[0].payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, frames[0].payload[1]);
}

void test_responder_break_received_separately(void)
{
    minlin_frame     frames[1] = {{.id = 0x12, .type = MINLIN_SUBSCRIBE, .payload = {0}, .payload_size = 2}};
    minlin_responder resp;
    minlin_responder_init(&resp);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_set_frames(&resp, frames, 1));

    // BREAK reported with the break character (0x00), then SYNC as a separate byte.
    const uint8_t bytes[6] = {0x00, 0x55, 0x92, 0x10, 0x20, 0x3D};
    s_receive_frame(&resp, bytes, sizeof(bytes));
    g_responder_subscribe_expected = (minlin_frame){
        .id = 0x12, .type = MINLIN_SUBSCRIBE, .payload = {0x10, 0x20}, .payload_size = 2};
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_task(&resp));
    TEST_ASSERT_TRUE(g_responder_subscribe_called);

    // Something else than SYNC after separately reported BREAK is a malformed SYNC.
    minlin_hw_transmit_break();
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_receive_byte(&resp, 0x00));
    TEST_ASSERT_EQUAL(MINLIN_RES_MALFORMED_SYNC, minlin_responder_receive_byte(&resp, 0x92));
    TEST_ASSERT_EQUAL(MINLIN_RESPONDER_IDLE, resp.state);
}

void test_responder_break_during_publish(void)
{
    // Regression: BREAK received while the task was in the publish callback caused NULL dereference.
    minlin_frame     frames[1] = {{.id = 0x11, .type = MINLIN_PUBLISH, .payload = {0}, .payload_size = 4}};
    minlin_responder resp;
    minlin_responder_init(&resp);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_set_frames(&resp, frames, 1));

    const uint8_t header[2] = {0x55, 0x11};
    s_receive_frame(&resp, header, sizeof(header));
    TEST_ASSERT_EQUAL(MINLIN_RESPONDER_RESPOND, resp.state);

    const uint8_t sync[1] = {0x55};
    s_inject_resp         = &resp;
    s_inject_bytes        = sync;
    s_inject_len          = sizeof(sync);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_task(&resp));
    // The response is dropped as it's too late, and the new frame's state is not overwritten.
    TEST_ASSERT_EQUAL(0, g_io_data_len);
    TEST_ASSERT_EQUAL(MINLIN_RESPONDER_WAIT_PID, resp.state);
}

void test_responder_break_during_subscribe(void)
{
    minlin_frame     frames[1] = {{.id = 0x12, .type = MINLIN_SUBSCRIBE, .payload = {0}, .payload_size = 2}};
    minlin_responder resp;
    minlin_responder_init(&resp);
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_set_frames(&resp, frames, 1));

    const uint8_t frame1[5] = {0x55, 0x92, 0x10, 0x20, 0x3D};
    s_receive_frame(&resp, frame1, sizeof(frame1));
    TEST_ASSERT_EQUAL(MINLIN_RESPONDER_RECEIVE_DONE, resp.state);

    // Next frame starts and its payload starts arriving while the subscribe callback is running.
    const uint8_t frame2[3]        = {0x55, 0x92, 0x77};
    s_inject_resp                  = &resp;
    s_inject_bytes                 = frame2;
    s_inject_len                   = sizeof(frame2);
    g_responder_subscribe_expected = (minlin_frame){
        .id = 0x12, .type = MINLIN_SUBSCRIBE, .payload = {0x10, 0x20}, .payload_size = 2};
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_task(&resp));
    TEST_ASSERT_TRUE(g_responder_subscribe_called);
    // The new frame's reception continues and is not overwritten by the task.
    TEST_ASSERT_EQUAL(MINLIN_RESPONDER_RECEIVE, resp.state);
    TEST_ASSERT_EQUAL_PTR(&frames[0], resp.current_frame);
    TEST_ASSERT_EQUAL_HEX8(0x10, frames[0].payload[0]);

    g_responder_subscribe_called              = false;
    g_responder_subscribe_expected.payload[0] = 0x77;
    g_responder_subscribe_expected.payload[1] = 0x88;
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_receive_byte(&resp, 0x88));
    TEST_ASSERT_EQUAL(
        MINLIN_RES_OK,
        minlin_responder_receive_byte(
            &resp, minlin_checksum(&resp.config, 0x12, (const uint8_t[]){0x77, 0x88}, 2, NULL)));
    TEST_ASSERT_EQUAL(MINLIN_RES_OK, minlin_responder_task(&resp));
    TEST_ASSERT_TRUE(g_responder_subscribe_called);
}

void minlin_responder_subscribe_callback(const minlin_frame* frame)
{
    s_inject_if_requested();
    g_responder_subscribe_called = true;
    TEST_ASSERT_EQUAL(g_responder_subscribe_expected.id, frame->id);
    TEST_ASSERT_EQUAL(g_responder_subscribe_expected.type, frame->type);
    TEST_ASSERT_EQUAL(g_responder_subscribe_expected.payload_size, frame->payload_size);
    if (frame->payload_size > 0) {
        TEST_ASSERT_EQUAL_HEX8_ARRAY(g_responder_subscribe_expected.payload, frame->payload, frame->payload_size);
    }
}

bool minlin_responder_publish_callback(minlin_frame* frame)
{
    s_inject_if_requested();
    if (frame->id == 0x11) {
        TEST_ASSERT_EQUAL(MINLIN_PUBLISH, frame->type);
        TEST_ASSERT_EQUAL(4, frame->payload_size);
        frame->payload[0] = 0x01;
        frame->payload[1] = 0x02;
        frame->payload[2] = 0x03;
        frame->payload[3] = 0x04;
        return true;
    }
    if (frame->id == 0x06) {
        TEST_ASSERT_EQUAL(MINLIN_PUBLISH, frame->type);
        TEST_ASSERT_EQUAL(8, frame->payload_size);
        frame->payload[0] = 0x11;
        frame->payload[1] = 0x12;
        frame->payload[2] = 0x13;
        frame->payload[3] = 0x14;
        frame->payload[4] = 0x15;
        frame->payload[5] = 0x16;
        frame->payload[6] = 0x17;
        frame->payload[7] = 0x18;
        return true;
    }
    return false;
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_id_to_pid);
    RUN_TEST(test_checksum);
    RUN_TEST(test_commander);
    RUN_TEST(test_responder);
    RUN_TEST(test_commander_set_schedule_validation);
    RUN_TEST(test_commander_restart_after_stop);
    RUN_TEST(test_commander_jitter_compensation);
    RUN_TEST(test_responder_set_frames_validation);
    RUN_TEST(test_responder_set_frames_drops_partial_header);
    RUN_TEST(test_responder_bad_checksum_keeps_payload);
    RUN_TEST(test_responder_break_received_separately);
    RUN_TEST(test_responder_break_during_publish);
    RUN_TEST(test_responder_break_during_subscribe);
    return UNITY_END();
}
