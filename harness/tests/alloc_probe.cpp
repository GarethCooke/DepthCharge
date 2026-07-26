// alloc_probe.cpp — global operator new/delete replacement for the test binary.
//
// Deliberately does NOT replace the over-aligned (std::align_val_t) forms: the
// library's own aligned pair stays consistent, and aligned_alloc is not portable
// to every toolchain this repo has to build on (MinGW-w64). Nothing on the
// engine feed path is over-aligned, so the measurement is unaffected.
#include "alloc_probe.hpp"

#include <cstdlib>
#include <new>

namespace {
std::size_t g_allocations = 0;

void* checked_malloc(std::size_t n) {
    // malloc(0) may legitimately return nullptr; ask for a byte so the returned
    // pointer is always distinct and freeable.
    void* p = std::malloc(n == 0 ? 1 : n);
    return p;
}
}  // namespace

namespace dc::testing {
std::size_t allocation_count() noexcept { return g_allocations; }
}  // namespace dc::testing

void* operator new(std::size_t n) {
    ++g_allocations;
    void* p = checked_malloc(n);
    if (p == nullptr) { throw std::bad_alloc(); }
    return p;
}

void* operator new[](std::size_t n) {
    ++g_allocations;
    void* p = checked_malloc(n);
    if (p == nullptr) { throw std::bad_alloc(); }
    return p;
}

void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    ++g_allocations;
    return checked_malloc(n);
}

void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    ++g_allocations;
    return checked_malloc(n);
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
