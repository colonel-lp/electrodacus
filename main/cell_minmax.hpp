#pragma once

#include <cstdint>

// Rolling per-cell voltage extremes over roughly the last 24 hours.
//
// The window is split into 24 one-hour buckets; each bucket keeps the min and
// max cell voltage seen during that hour. As uptime advances into a new hour the
// bucket that is about to be reused (it holds data from ~24 h ago) is cleared, so
// low()/high() always aggregate between 23 and 24 hours of history.
//
// All state lives in RAM and is not persisted: a reboot starts the window over,
// same as the daily-e-mail scheduler. Every method is meant to be called from a
// single task (the main loop); there is no internal locking.
class CellMinMax {
public:
    static constexpr int CELLS = 8;
    static constexpr int BUCKETS = 24; // one hour each

    CellMinMax();

    // Fold one SBMS frame into the history. `now_ms` is monotonic uptime
    // (millis()). Cell voltages of 0 mV are treated as "cell absent" and ignored.
    void update(const uint16_t cellVoltageMV[CELLS], uint32_t now_ms);

    // Drop all history and restart the window at `now_ms`.
    void reset(uint32_t now_ms);

    // Lowest / highest voltage retained for `cell` (0..CELLS-1), in mV.
    // Returns 0 when no sample has been recorded for that cell yet.
    uint16_t low(int cell) const;
    uint16_t high(int cell) const;

private:
    struct Bucket {
        uint16_t min; // 0xFFFF when empty
        uint16_t max; // 0 when empty
    };

    void clearBucket(int idx);

    Bucket buckets_[CELLS][BUCKETS];
    uint32_t lastEpochHours_; // now_ms / 3600000 at the last update
    bool haveEpoch_;
};
