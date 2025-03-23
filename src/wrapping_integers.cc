#include "wrapping_integers.hh"

#include "debug.hh"

using namespace std;

Wrap32 Wrap32::wrap(uint64_t n, Wrap32 zero_point) { return zero_point + n; }

uint64_t Wrap32::unwrap(Wrap32 zero_point, uint64_t checkpoint) const {
    uint64_t temp =
        ((uint64_t)this->raw_value_ + ((1UL << 32)) - (uint64_t)zero_point.raw_value_) % (1UL << 32) +
        (checkpoint / (1UL << 32)) * (1UL << 32);
    if (temp == checkpoint) {
        return temp;
    } else if (temp < checkpoint) {
        uint64_t distance1 = checkpoint - temp;
        uint64_t distance2 = temp + (1UL << 32) - checkpoint;
        return distance1 < distance2 ? temp : temp + (1UL << 32);
    } else {
        uint64_t distance1 = temp - checkpoint;
        uint64_t distance2 = (checkpoint + (1UL << 32)) - temp;
        return distance1 < distance2 ? temp : (temp < (1UL << 32) ? temp : temp - (1UL << 32));
    }
}
