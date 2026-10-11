#include "minlin/minlin_hw.h"

// Global LIN bus IO data buffer for unit testing purposes
extern uint8_t g_io_data[32];
extern uint8_t g_io_data_len;

// Current critical section depth and whether nesting (or unbalanced exit) has been detected.
int  g_critical_depth = 0;
bool g_critical_error = false;

static bool s_io_break = false;

bool minlin_hw_check_and_clear_break(void)
{
    bool had_break = s_io_break;
    s_io_break     = false;
    return had_break;
}

bool minlin_hw_transmit_break(void)
{
    s_io_break = true;
    return true;
}

bool minlin_hw_transmit_data(const uint8_t* data, uint8_t data_size)
{
    if (g_critical_depth != 0) {
        g_critical_error = true;
    }
    for (uint8_t i = 0; i < data_size; ++i) {
        if (g_io_data_len >= 32) {
            return false;
        }
        g_io_data[g_io_data_len] = data[i];
        g_io_data_len++;
    }
    return true;
}

void minlin_hw_enter_critical(void)
{
    if (g_critical_depth != 0) {
        g_critical_error = true;
    }
    g_critical_depth++;
}

void minlin_hw_exit_critical(void)
{
    if (g_critical_depth != 1) {
        g_critical_error = true;
    }
    g_critical_depth--;
}
