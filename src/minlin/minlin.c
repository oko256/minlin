/**
 * SPDX-License-Identifier: MIT
 * Copyright 2026 Olli Kokko <https://github.com/oko256/minlin>
 */

#include "minlin/minlin.h"
#include "minlin/minlin_hw.h"

#define LIN_CHECKSUM_WRAP 0xFF

#define ID_BIT(id, n) (((id) >> (n)) & 0x01)

static void s_set_res(minlin_res res, minlin_res* out_res)
{
    if (out_res != NULL) {
        *out_res = res;
    }
}

static void s_copy_bytes(uint8_t* dst, const uint8_t* src, uint8_t size)
{
    for (uint8_t i = 0; i < size; ++i) {
        dst[i] = src[i];
    }
}

uint8_t minlin_id_to_pid(uint8_t id, minlin_res* out_res)
{
    if (id > MINLIN_MAX_LIN_ID) {
        s_set_res(MINLIN_RES_ID_OUT_OF_BOUNDS, out_res);
        return 0;
    }

    // Calculate the parity from the bits of the ID, involves "magic numbers" as the bit positions.
    // NOLINTBEGIN(*-magic-numbers)
    const uint8_t p0 = ID_BIT(id, 0) ^ ID_BIT(id, 1) ^ ID_BIT(id, 2) ^ ID_BIT(id, 4);
    const uint8_t p1 = ID_BIT(id, 1) ^ ID_BIT(id, 3) ^ ID_BIT(id, 4) ^ ID_BIT(id, 5) ^ 0x01;
    s_set_res(MINLIN_RES_OK, out_res);
    return (uint8_t)(id | (p0 << 6) | (p1 << 7));
    // NOLINTEND(*-magic-numbers)
}

uint8_t minlin_checksum(
    const minlin_config* config, uint8_t id, const uint8_t* payload, uint8_t payload_size, minlin_res* out_res)
{
    if (config == NULL || payload == NULL) {
        s_set_res(MINLIN_RES_NULL_PARAM, out_res);
        return 0;
    }
    if (payload_size > MINLIN_MAX_PAYLOAD_BYTES) {
        s_set_res(MINLIN_RES_INVALID_PAYLOAD_SIZE, out_res);
        return 0;
    }

    minlin_res tmp_res  = MINLIN_RES_OK;
    uint16_t   checksum = 0;

    if (!config->classic_checksum) {
        const bool is_diag_frame = (id == MINLIN_DIAG_REQUEST_ID || id == MINLIN_DIAG_RESPONSE_ID);
        if (config->diagnostic_frames_as_normal_frames || !is_diag_frame) {
            // Use enhanced checksum: include PID in calculation
            checksum = minlin_id_to_pid(id, &tmp_res);
            if (tmp_res != MINLIN_RES_OK) {
                s_set_res(tmp_res, out_res);
                return 0;
            }
        }
    }

    for (uint8_t i = 0; i < payload_size; ++i) {
        checksum += payload[i];
        if (checksum > LIN_CHECKSUM_WRAP) {
            checksum -= LIN_CHECKSUM_WRAP;
        }
    }

    s_set_res(MINLIN_RES_OK, out_res);
    return (uint8_t)(LIN_CHECKSUM_WRAP - checksum);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Commander Functionality
///////////////////////////////////////////////////////////////////////////////////////////////////

static bool s_commander_transmit_header(uint8_t pid)
{
    const uint8_t header[2] = {MINLIN_SYNC_BYTE, pid};
    if (!minlin_hw_transmit_break()) {
        return false;
    }
    return minlin_hw_transmit_data(header, sizeof(header));
}

static void s_commander_next_entry(minlin_commander* self)
{
    self->next_entry_index++;
    if (self->next_entry_index >= self->schedule_entry_count) {
        if (self->loop) {
            self->next_entry_index = 0;
        } else {
            self->stopped = true;
        }
    }
}

void minlin_commander_init(minlin_commander* self)
{
    if (self == NULL) {
        return;
    }
    self->next_entry_index             = 0;
    self->schedule_entry_count         = 0;
    self->schedule_entries             = NULL;
    self->frame_slot_time_left_ms      = 0;
    self->max_jitter_ms                = 0;
    self->last_task_call_tick_count_ms = 0;
    self->loop                         = false;
    self->stopped                      = false;
}

minlin_res minlin_commander_set_schedule(
    minlin_commander* self, const minlin_schedule_entry* new_schedule, uint8_t new_schedule_count, bool loop)
{
    if (self == NULL) {
        return MINLIN_RES_NULL_PARAM;
    }
    if (new_schedule == NULL && new_schedule_count > 0) {
        return MINLIN_RES_NULL_PARAM;
    }
    for (uint8_t i = 0; i < new_schedule_count; ++i) {
        if (new_schedule[i].id > MINLIN_MAX_LIN_ID) {
            return MINLIN_RES_ID_OUT_OF_BOUNDS;
        }
        if (new_schedule[i].frame_slot_ms == 0) {
            return MINLIN_RES_INVALID_SLOT_TIME;
        }
    }
    self->next_entry_index     = 0;
    self->schedule_entry_count = new_schedule_count;
    self->schedule_entries     = new_schedule;
    self->loop                 = loop;
    self->stopped              = false;
    return MINLIN_RES_OK;
}

minlin_res minlin_commander_task(minlin_commander* self, uint32_t tick_count_ms)
{
    if (self == NULL) {
        return MINLIN_RES_NULL_PARAM;
    }

    const uint32_t time_since_last_call_ms = tick_count_ms - self->last_task_call_tick_count_ms;
    self->last_task_call_tick_count_ms     = tick_count_ms;

    // Frame slot is counted down even without active schedule, so that a slot that has already
    // expired won't delay the start of a new schedule.
    // Overshoot is how late this call is from the moment the slot expired; if it is within the
    // jitter budget, it is compensated from the next frame slot so that the schedule doesn't drift.
    uint32_t overshoot_ms = 0;
    if (self->frame_slot_time_left_ms > time_since_last_call_ms) {
        self->frame_slot_time_left_ms -= time_since_last_call_ms;
    } else if (self->frame_slot_time_left_ms > 0) {
        overshoot_ms                  = time_since_last_call_ms - self->frame_slot_time_left_ms;
        self->frame_slot_time_left_ms = 0;
    }

    if (self->schedule_entries == NULL || self->schedule_entry_count == 0) {
        // No active schedule, nothing to do.
        return MINLIN_RES_OK;
    }
    if (self->next_entry_index >= self->schedule_entry_count) {
        // Out of bounds index means to stop scheduling (looping disabled).
        return MINLIN_RES_OK;
    }

    if (self->frame_slot_time_left_ms == 0) {
        // Time to transmit the next entry in the current schedule.
        const minlin_schedule_entry* entry   = &(self->schedule_entries[self->next_entry_index]);
        minlin_res                   tmp_res = MINLIN_RES_OK;
        const uint8_t                pid     = minlin_id_to_pid(entry->id, &tmp_res);
        if (tmp_res != MINLIN_RES_OK) {
            s_commander_next_entry(self);
            return tmp_res;
        }
        // Frame slot duration is established here, so that even broken transmits won't flood the
        // network but instead honor the frame slot durations.
        if (overshoot_ms > self->max_jitter_ms || overshoot_ms >= entry->frame_slot_ms) {
            // Too late to compensate without squeezing this frame's slot; restart timing from now.
            overshoot_ms = 0;
        }
        self->frame_slot_time_left_ms = entry->frame_slot_ms - overshoot_ms;
        s_commander_next_entry(self);
        if (!s_commander_transmit_header(pid)) {
            return MINLIN_RES_TRANSMIT_FAILED;
        }
    }

    return MINLIN_RES_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Responder Functionality
///////////////////////////////////////////////////////////////////////////////////////////////////

static void s_responder_process_id(minlin_responder* self, uint8_t received_id)
{
    if (self->frames == NULL || self->frames_count == 0) {
        return;
    }
    for (uint8_t i = 0; i < self->frames_count; ++i) {
        if (self->frames[i].id == received_id) {
            if (self->frames[i].type == MINLIN_SUBSCRIBE) {
                self->current_frame  = &(self->frames[i]);
                self->payload_rx_pos = 0;
                self->state          = MINLIN_RESPONDER_RECEIVE;
                break;
            } else if (self->frames[i].type == MINLIN_PUBLISH) {
                self->current_frame = &(self->frames[i]);
                self->state         = MINLIN_RESPONDER_RESPOND;
                break;
            }
        }
    }
}

static minlin_res s_responder_process_break(minlin_responder* self, uint8_t received_byte)
{
    minlin_res res = MINLIN_RES_OK;
    if (self->state == MINLIN_RESPONDER_RECEIVE) {
        // No response was received for the frame we had subscribed for and received a header.
        res = MINLIN_RES_NO_RESPONSE;
    } else if (self->state == MINLIN_RESPONDER_RECEIVE_DONE) {
        // We received a response for frame we subscribed for, but we didn't manage to process
        // the data in time before another frame overrun it.
        res = MINLIN_RES_RECEIVE_OVERRUN;
    } else if (self->state != MINLIN_RESPONDER_IDLE) {
        // Waiting for sync or pid, or attempt to respond, was interrupted by sudden break.
        res = MINLIN_RES_UNEXPECTED_BREAK;
    }
    if (received_byte == MINLIN_SYNC_BYTE) {
        // HW does not report BREAK separately, so the BREAK flag was set when SYNC was received.
        self->state = MINLIN_RESPONDER_WAIT_PID;
    } else {
        // HW reported BREAK separately (e.g. the break character 0x00), SYNC byte comes next.
        self->state = MINLIN_RESPONDER_WAIT_SYNC;
    }
    self->current_frame = NULL;
    return res;
}

static minlin_res s_responder_respond(minlin_responder* self, minlin_frame* frame)
{
    minlin_res res = MINLIN_RES_OK;
    uint8_t    tx_buf[MINLIN_MAX_PAYLOAD_BYTES + 1];
    uint8_t    tx_size = 0;

    if (minlin_responder_publish_callback(frame)) {
        const uint8_t checksum = minlin_checksum(&self->config, frame->id, frame->payload, frame->payload_size, &res);
        if (res == MINLIN_RES_OK) {
            s_copy_bytes(tx_buf, frame->payload, frame->payload_size);
            tx_buf[frame->payload_size] = checksum;
            tx_size                     = frame->payload_size + 1;
        }
    }

    // Only respond if a new frame hasn't started meanwhile (receive function has then already
    // reported an unexpected break), and don't overwrite the state of that new frame.
    minlin_hw_enter_critical();
    const bool still_ours = (self->state == MINLIN_RESPONDER_RESPOND && self->current_frame == frame);
    if (still_ours) {
        self->state         = MINLIN_RESPONDER_IDLE;
        self->current_frame = NULL;
    }
    minlin_hw_exit_critical();

    if (still_ours && tx_size > 0 && !minlin_hw_transmit_data(tx_buf, tx_size)) {
        res = MINLIN_RES_TRANSMIT_FAILED;
    }
    return res;
}

void minlin_responder_init(minlin_responder* self)
{
    if (self == NULL) {
        return;
    }
    self->config.classic_checksum                   = false;
    self->config.diagnostic_frames_as_normal_frames = false;

    self->frames_count = 0;
    self->frames       = NULL;

    self->state          = MINLIN_RESPONDER_IDLE;
    self->current_frame  = NULL;
    self->payload_rx_pos = 0;
    for (uint8_t i = 0; i < MINLIN_MAX_PAYLOAD_BYTES; ++i) {
        self->rx_buf[i] = 0;
    }
}

minlin_res minlin_responder_set_frames(minlin_responder* self, minlin_frame* new_frames, uint8_t new_frames_count)
{
    if (self == NULL) {
        return MINLIN_RES_NULL_PARAM;
    }
    if (new_frames == NULL && new_frames_count > 0) {
        return MINLIN_RES_NULL_PARAM;
    }
    for (uint8_t i = 0; i < new_frames_count; ++i) {
        if (new_frames[i].id > MINLIN_MAX_LIN_ID) {
            return MINLIN_RES_ID_OUT_OF_BOUNDS;
        }
        if (new_frames[i].payload_size == 0 || new_frames[i].payload_size > MINLIN_MAX_PAYLOAD_BYTES) {
            return MINLIN_RES_INVALID_PAYLOAD_SIZE;
        }
    }
    minlin_hw_enter_critical();
    // Drop any frame in progress, including a partially received header: the application may have
    // stopped feeding received bytes for a while (e.g. while disabled), so a header state could be
    // stale and would report the next BREAK as unexpected.
    self->state          = MINLIN_RESPONDER_IDLE;
    self->current_frame  = NULL;
    self->payload_rx_pos = 0;
    self->frames         = new_frames;
    self->frames_count   = new_frames_count;
    minlin_hw_exit_critical();
    return MINLIN_RES_OK;
}

minlin_res minlin_responder_receive_byte(minlin_responder* self, uint8_t received_byte)
{
    if (self == NULL) {
        return MINLIN_RES_NULL_PARAM;
    }

    // First handle possible BREAK and return early.
    if (minlin_hw_check_and_clear_break()) {
        // BREAK received; a new frame has started.
        return s_responder_process_break(self, received_byte);
    }

    minlin_res res = MINLIN_RES_OK;

    // Handle the received byte based on what the responder state is.
    switch (self->state) {
    case MINLIN_RESPONDER_IDLE: {
        // Do nothing if we receive random bytes while idle.
    } break;
    case MINLIN_RESPONDER_WAIT_SYNC: {
        // Expecting a SYNC byte: validate it.
        if (received_byte != MINLIN_SYNC_BYTE) {
            res         = MINLIN_RES_MALFORMED_SYNC;
            self->state = MINLIN_RESPONDER_IDLE;
        } else {
            self->state = MINLIN_RESPONDER_WAIT_PID;
        }
    } break;
    case MINLIN_RESPONDER_WAIT_PID: {
        // Expecting a PID header byte: check if we are interested in it.
        const uint8_t received_id = (received_byte & MINLIN_MAX_LIN_ID);
        if (minlin_id_to_pid(received_id, NULL) != received_byte) {
            // Parity bits are not correct.
            res         = MINLIN_RES_MALFORMED_PARITY;
            self->state = MINLIN_RESPONDER_IDLE;
        } else {
            // Parity bits are OK.
            s_responder_process_id(self, received_id);
        }
        if (self->state != MINLIN_RESPONDER_RECEIVE && self->state != MINLIN_RESPONDER_RESPOND) {
            // We didn't care about this frame.
            self->state = MINLIN_RESPONDER_IDLE;
        }
    } break;
    case MINLIN_RESPONDER_RECEIVE: {
        const minlin_frame* frame = self->current_frame;
        if (self->payload_rx_pos == frame->payload_size) {
            // We have now received the checksum byte since the payload buffer is already full.
            const uint8_t checksum = minlin_checksum(&self->config, frame->id, self->rx_buf, frame->payload_size, &res);
            if (res != MINLIN_RES_OK) {
                // Something went wrong in the checksum calculation.
                self->state         = MINLIN_RESPONDER_IDLE;
                self->current_frame = NULL;
            } else if (checksum == received_byte) {
                // Received OK: periodic task can now offer the buffer to user's callback.
                self->state = MINLIN_RESPONDER_RECEIVE_DONE;
            } else {
                // Malformed checksum.
                res                 = MINLIN_RES_MALFORMED_CHECKSUM;
                self->state         = MINLIN_RESPONDER_IDLE;
                self->current_frame = NULL;
            }
        } else {
            // This is a payload byte.
            self->rx_buf[self->payload_rx_pos] = received_byte;
            self->payload_rx_pos++;
        }
    } break;
    case MINLIN_RESPONDER_RECEIVE_DONE:
    case MINLIN_RESPONDER_RESPOND:
        // Don't do anything in these states here; periodic task needs to do its work.
        break;
    }

    return res;
}

minlin_res minlin_responder_task(minlin_responder* self)
{
    if (self == NULL) {
        return MINLIN_RES_NULL_PARAM;
    }

    // Take the frame from the receive side in a critical section; the receive function may
    // otherwise change the state and the current frame at any point.
    minlin_hw_enter_critical();
    const minlin_responder_state state = self->state;
    minlin_frame* const          frame = self->current_frame;
    if (state == MINLIN_RESPONDER_RECEIVE_DONE) {
        s_copy_bytes(frame->payload, self->rx_buf, frame->payload_size);
        self->state         = MINLIN_RESPONDER_IDLE;
        self->current_frame = NULL;
    }
    minlin_hw_exit_critical();

    if (state == MINLIN_RESPONDER_RECEIVE_DONE) {
        minlin_responder_subscribe_callback(frame);
    } else if (state == MINLIN_RESPONDER_RESPOND) {
        return s_responder_respond(self, frame);
    }
    return MINLIN_RES_OK;
}
