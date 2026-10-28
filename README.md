# minLIN - Minimalistic LIN bus library

minLIN is a deliberately minimalistic LIN bus library in C mainly targeting embedded devices but
completely usable on other platforms as well. Proper usage example is the
[firmware for USB LIN Card](https://github.com/oko256/usb-lin-card-fw).

Supported LIN features:

- Commander (master) node: transmits frame headers (BREAK + SYNC + PID) according to a schedule
  table with per-entry frame slot durations, looping or one-shot, with optional jitter compensation.
- Responder (slave) node: subscribes to frame responses (with checksum validation) and publishes
  own responses to received headers.
- A node can be both commander and responder at the same time, e.g. to send command frames by
  scheduling the header and publishing the response itself.
- Classic (LIN 1.x) and enhanced (LIN 2.x) checksums; diagnostic frames (0x3C, 0x3D) use classic
  checksums by default.
- Error reporting for malformed SYNC, parity and checksum, missing responses, receive overruns,
  unexpected BREAKs, and failed transmits. A missing response or a receive overrun is detected
  when the next BREAK arrives.

Not supported (yet): event-triggered and sporadic frames, sleep/wake-up handling, transport
layer and diagnostic services, and multiple LIN interfaces.

## Integration

The library is hardware independent. Implement the hooks declared in `minlin_hw.h` for your
platform (see `examples/stm32_minlin_hw.c` for STM32 HAL):

- `minlin_hw_check_and_clear_break()`: report (and clear) a detected BREAK.
- `minlin_hw_transmit_break()` and `minlin_hw_transmit_data()`: transmit to the LIN bus, either
  blocking or queued (copy the data, the pointer is only valid during the call).
- `minlin_hw_enter_critical()` and `minlin_hw_exit_critical()`: block the interrupt that calls
  `minlin_responder_receive_byte()`, e.g. by disabling interrupts, and act as a compiler memory
  barrier. They are never nested. These can be empty if everything runs in the same context.

And the responder callbacks declared in `minlin.h`:

- `minlin_responder_subscribe_callback()`: handle a received frame that the node subscribes to.
- `minlin_responder_publish_callback()`: fill in the payload of a frame that the node publishes.

Then:

1. Initialize with `minlin_commander_init()` (if commander) and `minlin_responder_init()`, adjust
   `minlin_responder::config` if needed, and set the tables with `minlin_commander_set_schedule()`
   and `minlin_responder_set_frames()`. The tables are referenced, not copied.
2. Call `minlin_responder_receive_byte()` for each received byte, typically from the UART receive
   interrupt. Both BREAK reporting styles are supported: either the BREAK flag is already set when
   the SYNC byte arrives, or the BREAK is reported separately with a `0x00` byte before SYNC.
3. Call `minlin_commander_task()` (if commander) and then `minlin_responder_task()` frequently, for
   example in the main loop. The responder task also sends the responses, so the time between
   received header and the next `minlin_responder_task()` call adds directly to the response
   space.

The transceiver echoes all transmitted bytes back to the receiver, which is how a node that is both
commander and responder sees its own headers.

## Development setup

Following tools are required for the development of the library itself:

```bash
sudo apt install build-essential cmake cppcheck clang-tidy-19 lcov pipx ninja-build doxygen
pipx install fastcov
```

You should build with these CMake options:

- `minlin_DEVELOPER_MODE=ON`
- `minlin_BUILD_TESTING=ON`
- `minlin_COVERAGE=ON`

For example (you can also use classic `make` if you don't want to use `ninja`):

```bash
mkdir build
cd build
cmake -Dminlin_DEVELOPER_MODE=ON -Dminlin_BUILD_TESTING=ON -Dminlin_COVERAGE=ON -G Ninja ..
ninja
# Run unit tests:
ctest
# Create code coverage report:
ninja coverage
# Build and open documentation:
ninja doc
xdg-open html/index.html
```
