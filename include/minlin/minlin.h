/**
 * @file
 * @brief Main header file for minLIN library
 *
 * SPDX-License-Identifier: MIT
 * Copyright 2026 Olli Kokko <https://github.com/oko256/minlin>
 */

#pragma once

#include "minlin_export.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Frame ID 0x3C is used for diagnostic requests.
#define MINLIN_DIAG_REQUEST_ID   0x3C
/// Frame ID 0x3D is used for diagnostic responses.
#define MINLIN_DIAG_RESPONSE_ID  0x3D
/// LIN frames have at most 8 bytes of payload.
#define MINLIN_MAX_PAYLOAD_BYTES 8
/// All LIN frames start with break and this sync byte.
#define MINLIN_SYNC_BYTE         0x55
/// LIN frames have 6 bits for ID and 2 parity bits.
#define MINLIN_MAX_LIN_ID        0x3F

/// Determines size of statically reserved arrays.
#define MINLIN_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/// These parameters define the operational characteristics of the LIN network.
/// They should be set based on what kind of LIN network the node is operating in.
/// Initializing this struct to all zeros ({0}) corresponds to default modern LIN settings.
typedef struct {
    /// If @c true, uses classic LIN 1.x checksum calculation based on the payload bytes only,
    /// otherwise uses enhanced LIN 2.x checksum calculation that also includes the PID.
    bool classic_checksum;

    /// If @c true, IDs 60 (0x3C) and 61 (0x3D) are NOT considered as diagnostic frames that always
    /// use classic checksum calculation even when otherwise using enhanced checksums as per
    /// modern LIN standard.
    bool diagnostic_frames_as_normal_frames;
} minlin_config;

/// Result codes that can be returned from various minlin functions.
typedef enum {
    MINLIN_RES_OK = 0,
    MINLIN_RES_ID_OUT_OF_BOUNDS,
    MINLIN_RES_NULL_PARAM,
    MINLIN_RES_INVALID_PAYLOAD_SIZE,
    MINLIN_RES_INVALID_SLOT_TIME,
    MINLIN_RES_TRANSMIT_FAILED,
    MINLIN_RES_UNEXPECTED_BREAK,
    MINLIN_RES_NO_RESPONSE,
    MINLIN_RES_RECEIVE_OVERRUN,
    MINLIN_RES_MALFORMED_SYNC,
    MINLIN_RES_MALFORMED_PARITY,
    MINLIN_RES_MALFORMED_CHECKSUM
} minlin_res;

/// Frame type determines what happens when responder node receives a PID header for a frame that
/// it has a frame entry for.
typedef enum {
    MINLIN_SUBSCRIBE, ///< Node wants to receive any response that is sent for specified ID.
    MINLIN_PUBLISH ///< Node is going to publish its own response for the specified ID.
} minlin_frame_type;

/// Responder node's processing states.
typedef enum {
    MINLIN_RESPONDER_IDLE, ///< Waiting for next frame break to start a new frame.
    MINLIN_RESPONDER_WAIT_SYNC, ///< Waiting for sync byte.
    MINLIN_RESPONDER_WAIT_PID, ///< Waiting for PID byte.
    MINLIN_RESPONDER_RECEIVE, ///< Receiving payload bytes.
    MINLIN_RESPONDER_RECEIVE_DONE, ///< Payload received and frame is ready for user's processing.
    MINLIN_RESPONDER_RESPOND ///< Responding to the received header with own response data.
} minlin_responder_state;

/// Converts LIN ID to a PID header byte by calculating the parity bits into it.
/// Sets @p out_res to @c MINLIN_RES_OK on successful calculation.
/// @returns the PID header byte with the ID and the parity bits.
MINLIN_EXPORT uint8_t minlin_id_to_pid(uint8_t id, minlin_res* out_res);

/// Calculates the LIN checksum based on the LIN network configuration, the LIN ID (not PID),
/// and the payload data. Sets @p out_res to @c MINLIN_RES_OK on successful calculation.
/// @returns the calculated checksum byte.
MINLIN_EXPORT uint8_t minlin_checksum(
    const minlin_config* config, uint8_t id, const uint8_t* payload, uint8_t payload_size, minlin_res* out_res);

/// @name Commander Functionality
/// @{

/// Represents one entry in the commander node's schedule table. An array of these entries forms
/// a schedule, and commander node transmits the PID headers for frames according to the active
/// schedule.
typedef struct {
    /// Frame ID that should be sent for this schedule entry.
    uint8_t id;

    /// Frame slot duration is the time allocated for this entry's frame to be transmitted including
    /// the wait time until the next entry should be transmitted. It is the time from when this entry's
    /// header transmission is initiated until when the next frame's header transmission will be
    /// initiated.
    /// Frame slot duration should take into account the maximum transmission time of this frame
    /// (both header and possible response) including the possible master task jitter.
    /// See LIN spec sections 2.3.2 and 2.4.2 (\c T_frame_slot == \c frame_slot_ms):
    /// ```
    /// T_bit          = duration of single bit in the used bus speed (~0.05208 ms at 19.2 kbit/s)
    /// T_header_max   = 1.4 * (34 * T_bit)
    /// T_response_max = 1.4 * (10 * (N_data_bytes + 1) * T_bit)
    /// T_frame_max    = T_header_max + T_response_max
    /// T_frame_slot   > jitter + T_frame_max
    /// ```
    /// Must be greater than zero.
    uint32_t frame_slot_ms;
} minlin_schedule_entry;

/// Main struct that holds the commander node's state.
typedef struct {
    /// Number of entries in the currently active schedule entry array.
    uint8_t                      schedule_entry_count;
    /// Pointer to the currently active schedule entry array (does not take ownership of the array).
    const minlin_schedule_entry* schedule_entries;

    /// Next entry index in the schedule that will be processed after the current frame slot expires.
    uint8_t  next_entry_index;
    /// If this schedule should keep looping when end has been reached or not.
    bool     loop;
    /// If the schedule has been stopped because looping was disabled and it reached the end.
    /// This is set as soon as the header of the last entry has been transmitted, i.e. the frame
    /// slot of the last entry may still be running.
    bool     stopped;
    /// How much time is left for the current frame slot before processing the next entry.
    uint32_t frame_slot_time_left_ms;
    /// Maximum commander task jitter that has been taken into account in the frame slot durations
    /// (see @c minlin_schedule_entry::frame_slot_ms). If a task call is late by at most this much
    /// from when the frame slot expired, the next frame slot is shortened by the same amount so
    /// that the schedule doesn't drift. If the call is later than this, the next frame slot starts
    /// from the call instead, because shortening it more could make the frames collide.
    /// Set to @c 0 by @c minlin_commander_init(), which disables the compensation.
    uint32_t max_jitter_ms;

    /// Millisecond tick counter (clock) value when the processing task was called the last time.
    uint32_t last_task_call_tick_count_ms;
} minlin_commander;

/// Initializes the commander struct; this should be the first thing that's called to a new struct.
MINLIN_EXPORT void minlin_commander_init(minlin_commander* self);

/// Sets the schedule to be followed by the commander task. Schedule is an array of schedule entries.
/// Does not take the ownership of the schedule array, just refers to it via a pointer. This means
/// that the schedule shall outlive its use by the commander.
/// The first entry of the new schedule is transmitted when the frame slot of the currently
/// transmitted entry (if any) has expired.
/// Set @p new_schedule_count to @c 0 to disable scheduling.
/// Set @p loop to @c true to keep looping the schedule and @c false to just go through it once.
/// All entries are validated first; on error, the previously set schedule is kept unchanged.
/// Must not be called concurrently with @c minlin_commander_task() (call both from same context).
MINLIN_EXPORT minlin_res minlin_commander_set_schedule(
    minlin_commander* self, const minlin_schedule_entry* new_schedule, uint8_t new_schedule_count, bool loop);

/// Executes LIN commander node task; call this periodically to invoke LIN commander functionality.
/// Processing happens here on millisecond resolution, but this can be called more often than that
/// without any negative effects (extra calls are very cheap in terms of processing power).
/// Late calls within @c minlin_commander::max_jitter_ms are compensated, so that they do not
/// accumulate drift to the schedule.
/// @param tick_count_ms Value from a millisecond resolution tick counter, for example HAL_GetTick()
/// with standard 1 ms SysTick on STM32.
MINLIN_EXPORT minlin_res minlin_commander_task(minlin_commander* self, uint32_t tick_count_ms);

/// @}

/// @name Responder Functionality
/// @{

/// Represents a signal carrying frame that the responder node is interested to either subscribe to
/// or publish a response for.
typedef struct {
    /// Frame ID (not PID) of this frame.
    uint8_t           id;
    /// Whether this node subscribes to or publishes the response of this frame.
    minlin_frame_type type;
    /// Payload of the frame. Received payload is only copied here after it has been fully received
    /// with a valid checksum, and only by @c minlin_responder_task(), never by the receive function.
    uint8_t           payload[MINLIN_MAX_PAYLOAD_BYTES];
    /// Number of payload bytes in the frame (1-8).
    uint8_t           payload_size;
} minlin_frame;

/// Main struct that holds the responder node's state.
typedef struct {
    minlin_config config;

    /// Number of @c frames that this responder is interested in.
    uint8_t       frames_count;
    /// Pointer to array of frames that this responder is interested in (does not take ownership).
    minlin_frame* frames;

    /// Current state of the responder node.
    minlin_responder_state state;

    /// Pointer to the frame that is relevant to the current state (for receiving or responding).
    minlin_frame* current_frame;
    /// Current position in the receive buffer.
    uint8_t       payload_rx_pos;
    /// Receive buffer for the payload of the frame that is currently being received.
    uint8_t       rx_buf[MINLIN_MAX_PAYLOAD_BYTES];
} minlin_responder;

/// Initializes the LIN responder node struct; should be called as the first thing for new struct.
/// After this you can configure LIN network settings by changing the members of the @c config
/// under the @c minlin_responder struct.
MINLIN_EXPORT void minlin_responder_init(minlin_responder* self);

/// Sets the frames to be handled by the responder.
/// Does not take the ownership of the frames array, just a pointer to it.
/// Set @p new_frames_count to @c 0 to disable frame handling.
/// All frames are validated first; on error, the previously set frames are kept unchanged.
/// On success, any frame in progress (even a partially received header) is dropped, and the
/// responder waits for the next BREAK. Call this also when resuming after the received bytes have
/// not been fed to @c minlin_responder_receive_byte() for a while.
/// Safe to call while @c minlin_responder_receive_byte() may be called from an interrupt, but must
/// not be called concurrently with @c minlin_responder_task() (call both from same context).
MINLIN_EXPORT minlin_res
minlin_responder_set_frames(minlin_responder* self, minlin_frame* new_frames, uint8_t new_frames_count);

/// Process one byte of received data; to be called for each received byte on the LIN interface.
/// E.g. you could call this in the receive-completed interrupt callback of the LIN interface when
/// receiving one byte at a time.
/// BREAK detection is done with @c minlin_hw_check_and_clear_break(). Both of these HW behaviors
/// are supported:
/// - BREAK flag is already set when the SYNC byte is received (no separate call for BREAK).
/// - BREAK is reported separately before SYNC: call this function with @c 0x00 for the BREAK
///   (or pass the received break character, which is @c 0x00), and again for the SYNC byte.
MINLIN_EXPORT minlin_res minlin_responder_receive_byte(minlin_responder* self, uint8_t received_byte);

/// Executes LIN responder node task; call this periodically to invoke LIN responder functionality.
/// User callbacks are called from here, never from @c minlin_responder_receive_byte().
/// The cadence of these calls determine the maximum delay between received header and when the
/// response transmission starts ("response space"), so should be called often enough. E.g. at
/// 19.2 kbit/s with 8 payload bytes, the LIN spec leaves only ~1.9 ms margin for response space
/// and inter-byte spaces combined. This can be called from the main loop, or from an interrupt
/// with lower priority than the one calling @c minlin_responder_receive_byte(). On a node that is
/// both commander and responder, call this right after @c minlin_commander_task().
MINLIN_EXPORT minlin_res minlin_responder_task(minlin_responder* self);

/// @}

/// @name User-Defined Responder Hooks
/// @{

/// User's callback function to handle received frame that the node was subscribed to.
/// You should implement this as your own callback function to process the received frame.
/// You can read the received payload from the @p frame parameter. The payload stays intact until
/// the next frame with the same ID has been successfully received and @c minlin_responder_task()
/// is called again.
void minlin_responder_subscribe_callback(const minlin_frame* frame);

/// User's callback function to generate the intended response payload to a frame that the node
/// should publish. When this is called by the library, @p frame->id and @p frame->payload_size
/// are set, and you should populate @p frame->payload with the payload that you want to send for
/// the specific frame ID.
/// This function should return as soon as possible after setting the payload; don't do any
/// excessively long processing in this function.
/// Return @c true if the response should be transmitted, or @c false if you want to skip responding.
bool minlin_responder_publish_callback(minlin_frame* frame);

/// @}

#ifdef __cplusplus
}
#endif
