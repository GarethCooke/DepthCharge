// alloc_probe.hpp — counts heap allocations so invariant #7 can be *tested*.
//
// "Allocation-free steady state" is the kind of claim that quietly stops being
// true. Replacing the global operator new/delete in the test binary turns it
// into an assertion: take the counter, run the feed path, take it again, require
// no change. Only the engine path is measured — doctest, nlohmann and the
// console renderer all allocate freely and are simply kept outside the window.
#pragma once

#include <cstddef>

namespace dc::testing {

// Monotonic count of global operator new calls since process start.
std::size_t allocation_count() noexcept;

}  // namespace dc::testing
