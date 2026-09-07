// depthcharge/sample_window.hpp — the fixed ring, and the one median convention.
//
// Two instruments read the venue's liveness signal and answer different
// questions from it: `liveness_clock.hpp` decides when the panel greys, and
// `age_estimator.hpp` says how far behind the book is. Both keep a bounded
// window of samples and both take a median of one, so both used to carry their
// own copy of three lines of ring arithmetic and their own median.
//
// That is exactly the shape of defect this project keeps finding — two copies
// of a rule, drifting. The now-deleted `firmware/src/staleness.hpp` documented
// one: its `delivered_us()` was written twice and the two copies had already
// disagreed about `>=` versus `>` before anyone noticed, harmlessly, which is
// how near-duplicates survive long enough to matter. So the ring and the median
// live here, once, and the two instruments differ only in what they measure —
// which is the difference that is supposed to be visible.
//
// IT LIVES IN `engine/` BECAUSE BOTH ENDS RUN IT. Moved here from
// `harness/include/dc_harness/` at M4 stage D (2026-08-20) together with its two
// users, so the host harness and the firmware compute the book's age and the
// grey threshold with the same code rather than with two implementations that
// agreed by circumstance. ESP-IDF-free and allocation-free, which is what makes
// that possible: `engine/` is host-buildable by invariant #1 and every header in
// it is compiled by `dc_engine_target_check` under xtensa GCC 8.4 as well.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace depthcharge {

// A fixed-capacity ring of the last `N` samples, in arrival order.
//
// `at(0)` is the OLDEST retained sample, which is the indexing the age
// estimator's suffix scan needs and the only part of this that is easy to get
// subtly wrong once the ring has wrapped.
template <class T, std::size_t N>
class SampleRing {
public:
    void push(T value) noexcept {
        samples_[next_] = value;
        next_ = (next_ + 1) % N;
        if (count_ < N) { ++count_; }
    }

    void clear() noexcept {
        next_ = 0;
        count_ = 0;
    }

    std::size_t size() const noexcept { return count_; }
    bool full() const noexcept { return count_ == N; }

    // i = 0 is the oldest retained sample; i = size() - 1 the newest.
    T at(std::size_t i) const noexcept {
        const std::size_t oldest = full() ? next_ : 0;
        return samples_[(oldest + i) % N];
    }

private:
    std::array<T, N> samples_{};
    std::size_t next_ = 0;
    std::size_t count_ = 0;
};

// The LOWER median by nearest rank, over a caller-owned scratch buffer which
// this sorts in place. Returns 0 for an empty range.
//
// **THE SORT IS FULL AND ASCENDING, AND A CALLER DEPENDS ON THAT.**
// `harness/src/trace.cpp` reads `gaps.back()` as the maximum immediately after
// calling this, taking one ordering rather than two. Switching to
// `std::nth_element` would be a legitimate-looking optimisation on this
// function's own terms and would silently stop that `back()` being a maximum,
// in another file. Named at both ends because a cross-file coupling that only
// one end knows about is a coupling nobody knows about.
//
// NEAREST RANK RATHER THAN INTERPOLATION, and the convention matters enough to
// have one home: an interpolated median invents an interval that never occurred
// on the wire, and at these sample sizes the rank is the thing being measured.
// `tools/gap_stats.py` uses the same rule, so a C++ figure and a Python figure
// over the same trace are comparable — which is how several of this project's
// numbers have been checked.
inline double lower_median(double* first, std::size_t n) noexcept {
    if (n == 0) { return 0.0; }
    std::sort(first, first + n);
    return first[(n - 1) / 2];
}

}  // namespace depthcharge
