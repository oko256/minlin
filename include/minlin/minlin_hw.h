/**
 * @file
 * @brief Hardware integration layer for minLIN library
 *
 * Contains prototypes of the functions that should be implemented by the user for the specific
 * platform they are using this library with.
 *
 * The transmit functions may either block until the data has been sent, or queue the data for
 * transmission (e.g. interrupt or DMA driven), as long as the order of BREAK and data is preserved.
 * When queuing, note that the data pointers are only valid during the call, so the data must be
 * copied.
 *
 * SPDX-License-Identifier: MIT
 * Copyright 2026 Olli Kokko <https://github.com/oko256/minlin>
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Checks if transceiver has received BREAK, and clears the possible BREAK flag.
/// This is called from within @c minlin_responder_receive_byte(), so it runs in the same context
/// (usually the receive interrupt).
/// @returns @c true if break was received, otherwise @c false.
bool minlin_hw_check_and_clear_break(void);

/// Transmits BREAK to the LIN bus.
/// @returns @c true if transmit was successful, otherwise @c false.
bool minlin_hw_transmit_break(void);

/// Transmits bunch of bytes to the LIN bus.
/// @returns @c true if transmit was successful, otherwise @c false.
bool minlin_hw_transmit_data(const uint8_t* data, uint8_t data_size);

/// Enters a critical section where @c minlin_responder_receive_byte() cannot be called, i.e.
/// typically disables the interrupt (or all interrupts) where that function is called from.
/// Must also act as a compiler memory barrier (e.g. CMSIS @c __disable_irq() does).
/// The library never nests critical sections and never calls any other hook or callback while
/// inside one, so these are held only for very short durations.
/// If everything is called from the same context (no interrupts), this can be left empty.
void minlin_hw_enter_critical(void);

/// Exits the critical section entered with @c minlin_hw_enter_critical().
void minlin_hw_exit_critical(void);

#ifdef __cplusplus
}
#endif
