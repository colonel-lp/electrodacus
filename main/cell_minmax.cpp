#include "cell_minmax.hpp"

namespace {
constexpr uint32_t MS_PER_HOUR = 3600u * 1000u;
constexpr uint16_t EMPTY_MIN = 0xFFFF;
constexpr uint16_t EMPTY_MAX = 0;
} // namespace

CellMinMax::CellMinMax()
{
    reset(0);
    haveEpoch_ = false;
}

void CellMinMax::clearBucket(int idx)
{
    for(int c = 0; c < CELLS; c++)
    {
        buckets_[c][idx].min = EMPTY_MIN;
        buckets_[c][idx].max = EMPTY_MAX;
    }
}

void CellMinMax::reset(uint32_t now_ms)
{
    for(int i = 0; i < BUCKETS; i++) clearBucket(i);
    lastEpochHours_ = now_ms / MS_PER_HOUR;
    haveEpoch_ = true;
}

void CellMinMax::update(const uint16_t cellVoltageMV[CELLS], uint32_t now_ms)
{
    const uint32_t epoch = now_ms / MS_PER_HOUR;

    if(!haveEpoch_)
    {
        reset(now_ms);
    }
    else if(epoch != lastEpochHours_)
    {
        const uint32_t advanced = epoch - lastEpochHours_; // wraps -> large -> wipe all
        if(advanced >= BUCKETS)
        {
            for(int i = 0; i < BUCKETS; i++) clearBucket(i);
        }
        else
        {
            for(uint32_t e = lastEpochHours_ + 1; e != epoch + 1; e++)
            {
                clearBucket(static_cast<int>(e % BUCKETS));
            }
        }
        lastEpochHours_ = epoch;
    }

    const int idx = static_cast<int>(epoch % BUCKETS);
    for(int c = 0; c < CELLS; c++)
    {
        const uint16_t v = cellVoltageMV[c];
        if(v == 0) continue; // cell absent / not reported
        if(v < buckets_[c][idx].min) buckets_[c][idx].min = v;
        if(v > buckets_[c][idx].max) buckets_[c][idx].max = v;
    }
}

uint16_t CellMinMax::low(int cell) const
{
    if(cell < 0 || cell >= CELLS) return 0;
    uint16_t m = EMPTY_MIN;
    for(int i = 0; i < BUCKETS; i++)
    {
        if(buckets_[cell][i].min < m) m = buckets_[cell][i].min;
    }
    return m == EMPTY_MIN ? 0 : m;
}

uint16_t CellMinMax::high(int cell) const
{
    if(cell < 0 || cell >= CELLS) return 0;
    uint16_t m = EMPTY_MAX;
    for(int i = 0; i < BUCKETS; i++)
    {
        if(buckets_[cell][i].max > m) m = buckets_[cell][i].max;
    }
    return m;
}
