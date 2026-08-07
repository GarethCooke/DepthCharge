// alloc_probe.cpp — global operator new/delete replacement for the test binary.
//
// Deliberately does NOT replace the over-aligned (std::align_val_t) forms: the
// library's own aligned pair stays consistent, and aligned_alloc is not portable
// to every toolchain this repo has to build on (MinGW-w64). Nothing on the
// engine feed path is over-aligned, so the measurement is unaffected.
#include "alloc_probe.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
// Atomic since M3 stage A: test_snapshot_channel.cpp runs worker threads, and
// std::thread's own startup allocates. A plain counter would make every
// threaded test in the binary undefined behaviour — the exact defect the
// channel it is measuring was rebuilt to avoid. Relaxed is enough: the counter
// is only ever read on the main thread with the workers joined, so it needs to
// be exact, not ordered against anything.
std::atomic<std::size_t> g_allocations{0};

void* checked_malloc(std::size_t n) {
    // malloc(0) may legitimately return nullptr; ask for a byte so the returned
    // pointer is always distinct and freeable.
    void* p = std::malloc(n == 0 ? 1 : n);
    return p;
}
}  // namespace

namespace dc::testing {
std::size_t allocation_count() noexcept {
    return g_allocations.load(std::memory_order_relaxed);
}
}  // namespace dc::testing

namespace {
void count_one() noexcept { g_allocations.fetch_add(1, std::memory_order_relaxed); }
}  // namespace

void* operator new(std::size_t n) {
    count_one();
    void* p = checked_malloc(n);
    if (p == nullptr) { throw std::bad_alloc(); }
    return p;
}

void* operator new[](std::size_t n) {
    count_one();
    void* p = checked_malloc(n);
    if (p == nullptr) { throw std::bad_alloc(); }
    return p;
}

void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    count_one();
    return checked_malloc(n);
}

void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    count_one();
    return checked_malloc(n);
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
