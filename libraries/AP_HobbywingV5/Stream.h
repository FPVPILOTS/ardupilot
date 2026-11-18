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

    void reset() { used = 0; }
    uint32_t valid_count() const { return valid_frames; }
    uint32_t invalid_count() const { return invalid_frames; }

private:
    uint8_t buffer[TELEMETRY_FRAME_LENGTH]{};
    size_t used = 0;
    uint32_t valid_frames = 0;
    uint32_t invalid_frames = 0;
};

} // namespace HobbywingV5
