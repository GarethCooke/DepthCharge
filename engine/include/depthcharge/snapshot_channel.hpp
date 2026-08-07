// depthcharge/snapshot_channel.hpp — the feed -> render hand-off seam.
//
// ARCHITECTURE §5: DisplaySnapshot is "published by version-stamped double
// buffer; the render side takes the latest complete version and never blocks
// the writer". Invariant #4: the feed task is never blocked by the render task.
// Invariant #8: the two tasks meet here and nowhere else.
//
// M1 built the API and deferred the mechanism ("one slot, one version counter,
// single-threaded use only — the class is not a synchronisation primitive yet
// and does not pretend to be"). M3 stage A is the mechanism. The public face is
// unchanged — same four functions, same signatures, same semantics — because
// the whole point of pinning the seam at M1 was that this swap would not reach
// a caller.
//
// Contract: exactly one publishing thread (the feed task, Core 0) and exactly
// one consuming thread (the render task, Core 1). Two publishers or two
// consumers are undefined; this is an SPSC channel, not a general one.
//
//
// Why three slots and not two
// ---------------------------
// The M3 brief offered "two slots + an atomic version, or a classic seqlock".
// Both let the reader copy a slot the writer may be writing and then *discard*
// the result if the version moved underneath — the torn read happens, and the
// version check only stops it reaching the panel.
//
// That was measured rather than assumed, both ways. A seqlock of exactly that
// shape delivered 4.8 M frames with zero tears reaching the consumer (x86,
// GCC 10, -O2): it is not that a seqlock misbehaves on today's compiler.
// ThreadSanitizer flagged it on the first frame all the same — "Read of size 8
// ... previous write of size 8", consume() against publish() — because the copy
// *is* a data race on DisplaySnapshot however the retry loop then tidies up.
// Stage A's definition of done asks for a clean TSan report, and the only way
// to have both is a suppression that says "ignore the hand-off": the one place
// in this project where a suppression would sit is the one place a race would
// matter, on a target compiler generation (xtensa GCC 8.4) nobody here controls.
//
// Two slots cannot dodge it either: the reader is holding one of them, so a
// writer alternating across two must eventually land on the one being read.
//
// Three slots removes the possibility instead of detecting it after the fact.
// At every instant the writer's slot, the reader's slot and the ready slot are
// three distinct objects — the three indices are always a permutation of
// {0,1,2} — so no DisplaySnapshot is ever touched by both threads at once. The
// only shared mutable state is one 32-bit atomic word, and the TSan report is
// clean because there is no race to annotate away.
//
// Cost: 2,352 bytes more than M1's single slot — sizeof is 3,528 against 1,176,
// being three 1,168-byte frames plus 20 bytes of bookkeeping. That is 0.7% of
// the S3's 512 KB of internal SRAM, for a hand-off with no race to argue about.
//
//
// The word
// --------
//      ready_ = [ bit 2: FRESH ][ bits 1..0: slot index ]
//
//   publish()  fills slots_[write_], then exchanges ready_ <- write_|FRESH and
//              adopts whatever slot came back as its next write_.
//   consume()  returns false without touching a slot if FRESH is clear.
//              Otherwise it exchanges ready_ <- read_ (which also clears
//              FRESH), adopts the slot that came back, and copies it out.
//
// The exchange is what keeps the permutation true: each side gives up its old
// slot in the same indivisible step that takes the other's. Nothing else can
// change the word, so nothing else can break the invariant.
//
//
// Ordering
// --------
// Both exchanges are acq_rel, and one release/acquire edge is doing work in
// both directions:
//
//   forwards  the writer's release orders its fill of slots_[write_] before
//             that slot becomes reachable; the reader's acquire picks it up, so
//             the copy-out sees a complete frame and never a half-written one.
//   backwards the reader finished copying the slot it hands back during the
//             *previous* consume(), before this exchange — so the writer's
//             acquire orders those reads before it starts refilling that slot.
//
// The peek in consume() is acquire rather than relaxed so that every read of
// ready_ in this class carries the same ordering and the argument above only
// has to be made once; it costs nothing on either target. It exists purely so a
// render task with nothing to draw skips the read-modify-write as well as the
// redraw.
//
//
// Wait-freedom (invariant #4)
// ---------------------------
// publish() is a fixed-size copy plus one atomic exchange: no loop, no lock,
// and nothing the reader can hold that the writer has to wait for. On x86 the
// exchange is a single `xchg`. On the Xtensa LX7 GCC lowers it to an S32C1I
// compare-and-swap retry whose only possible contender is the single reader
// running its own one-shot exchange — bounded, and still nothing that lets a
// busy render task stall the feed task, which is what the invariant is about.
//
// There is no queue here, so there is no overflow to report as Gap{Overflow}: a
// reader that falls behind loses intermediate frames and is handed the newest
// one, which is what a ladder wants — a skipped frame is a frame the panel
// would have overdrawn anyway, and the trade ring inside DisplaySnapshot
// carries the last eight prints across the skip. Gap{Overflow} stays reserved
// for a bounded queue that actually drops, should one ever exist.
#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

#include "depthcharge/display_snapshot.hpp"

namespace depthcharge {

class SnapshotChannel {
public:
    // Feed side. Wait-free by contract — it must never block on the consumer.
    void publish(const DisplaySnapshot& snap) noexcept {
        // slots_[write_] is private to this thread until the exchange below
        // hands it over, so this copy needs no synchronisation of its own.
        slots_[write_] = snap;

        // Stamped before the frame is reachable, so a reader that has just
        // acquired it can never see consumed_version() run ahead of
        // published_version().
        published_.store(snap.version, std::memory_order_relaxed);

        const std::uint32_t prev = ready_.exchange(write_ | kFresh, std::memory_order_acq_rel);
        write_ = prev & kSlotMask;
    }

    // Render side. Copies the latest complete frame into `out` and returns true;
    // returns false if nothing has been published since the last consume, so a
    // render task can skip redrawing rather than spin.
    bool consume(DisplaySnapshot& out) noexcept {
        if ((ready_.load(std::memory_order_acquire) & kFresh) == 0u) { return false; }

        // A publish landing between the peek and this exchange is not a problem:
        // it only means the slot handed back here is a newer one than the peek
        // saw. Taking the latest is the contract, not a compromise of it.
        const std::uint32_t prev = ready_.exchange(read_, std::memory_order_acq_rel);
        read_ = prev & kSlotMask;

        out = slots_[read_];
        consumed_.store(out.version, std::memory_order_relaxed);
        return true;
    }

    // Latest published version without consuming — for stats and tests.
    std::uint32_t published_version() const noexcept {
        return published_.load(std::memory_order_relaxed);
    }
    std::uint32_t consumed_version() const noexcept {
        return consumed_.load(std::memory_order_relaxed);
    }

private:
    static constexpr std::uint32_t kSlots = 3;
    static constexpr std::uint32_t kSlotMask = 0x3u;
    static constexpr std::uint32_t kFresh = 0x4u;

    // Three is a correctness requirement, not a tuning knob, and these three
    // constants have to be edited together or `prev & kSlotMask` starts
    // indexing past the array. Two slots would reintroduce the race the whole
    // design exists to remove (see the header comment); more than three would
    // work but needs a wider mask and a moved flag, and buys nothing — the
    // reader only ever holds one frame.
    static_assert(kSlots == 3, "the rotation, the mask and the FRESH bit all assume three slots");
    static_assert(kSlots - 1 <= kSlotMask, "kSlotMask must cover every slot index");
    static_assert(kFresh == kSlotMask + 1, "FRESH must be the first bit above the index");

    // If this word ever needed a lock the writer would be blockable through it,
    // and invariant #4 would be a comment rather than a property. Checked here
    // rather than assumed, because it is the one platform fact the whole design
    // rests on — and the build compiles this header for the ESP32-S3 too.
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "SnapshotChannel needs a lock-free 32-bit atomic (invariant #4)");

    DisplaySnapshot slots_[kSlots]{};

    // Slot 2 is parked as the initial ready slot with FRESH clear, so the first
    // consume() before any publish() reports nothing-new rather than handing
    // back a default-constructed frame. write_ and read_ take the other two:
    // {0, 1, 2}, a permutation from the first instruction onwards.
    std::atomic<std::uint32_t> ready_{2};
    std::uint32_t write_ = 0;  // feed thread only — never read by the consumer
    std::uint32_t read_ = 1;   // render thread only — never read by the producer

    // Stats, not mechanism: nothing in publish/consume branches on these. Atomic
    // because either side may read either counter, and a plain uint32_t read
    // across threads is the same data race the slots were designed out of.
    std::atomic<std::uint32_t> published_{0};
    std::atomic<std::uint32_t> consumed_{0};
};

// The atomic members make the channel non-copyable, which is what it should have
// been all along: a channel is a rendezvous between two specific threads, and a
// copy of one is two channels with one thread each.
static_assert(!std::is_copy_constructible_v<SnapshotChannel>);

}  // namespace depthcharge
