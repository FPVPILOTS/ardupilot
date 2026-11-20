/*
 * Hobbywing Platinum V5 telemetry research support for ArduPilot.
 * Copyright and license: GNU General Public License v3 or later,
 * consistent with the ArduPilot and Rotorflight projects.
 *
 * Protocol field layout and serial framing are informed by Rotorflight:
 * https://github.com/rotorflight/rotorflight-firmware/blob/2a0a8d10712499651f00a817af5559bab6582b87/src/main/sensors/esc_sensor.c
 * Check original copyrights and preserve attribution if copying upstream code.
 * Bench-only experimental implementation; not yet flight validated.
 */
#pragma once

// Receive-only byte-stream framing for Hobbywing V5 telemetry, no HAL dependency.
// No UART writes or ESC commands, no heap allocation.
#include "Protocol.h"
#include <string.h>

namespace HobbywingV5 {

class Stream {
public:
    bool feed(uint8_t byte, Sample &out)
    {
        if (used == 0 && byte != 0xFD && byte != 0xFE) {
            return false;
        }
        buffer[used++] = byte;
        if (used < TELEMETRY_FRAME_LENGTH) {
            return false;
        }
        if (decode_telemetry(buffer, TELEMETRY_FRAME_LENGTH, out)) {
            memcpy(last_valid_frame, buffer, TELEMETRY_FRAME_LENGTH);
            has_last_valid_frame = true;
            valid_frames++;
            used = 0;
            return true;
        }
        invalid_frames++;
        // Recover from noise and truncated frames: retain the next potential start.
        size_t next = 1;
        while (next < TELEMETRY_FRAME_LENGTH &&
               buffer[next] != 0xFD && buffer[next] != 0xFE) {
            next++;
        }
        if (next < TELEMETRY_FRAME_LENGTH) {
            used = TELEMETRY_FRAME_LENGTH - next;
            memmove(buffer, buffer + next, used);
        } else {
            used = 0;
        }
        return false;
    }

    void reset() { used = 0; has_last_valid_frame = false; }

    // Return only a complete CRC-validated frame; never expose partial/noisy RX.
    bool copy_last_valid(uint8_t *out, size_t len) const
    {
        if (!has_last_valid_frame || out == nullptr || len < TELEMETRY_FRAME_LENGTH) {
            return false;
        }
        memcpy(out, last_valid_frame, TELEMETRY_FRAME_LENGTH);
        return true;
    }
    uint32_t valid_count() const { return valid_frames; }
    uint32_t invalid_count() const { return invalid_frames; }

private:
    uint8_t buffer[TELEMETRY_FRAME_LENGTH]{};
    uint8_t last_valid_frame[TELEMETRY_FRAME_LENGTH]{};
    bool has_last_valid_frame = false;
    size_t used = 0;
    uint32_t valid_frames = 0;
    uint32_t invalid_frames = 0;
};

} // namespace HobbywingV5
