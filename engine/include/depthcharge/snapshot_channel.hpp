// depthcharge/snapshot_channel.hpp — the feed -> render hand-off seam.
//
// ARCHITECTURE §5: DisplaySnapshot is "published by version-stamped double
// buffer; the render side takes the latest complete version and never blocks
// the writer". Invariant #4: the feed task is never blocked by the render task.
//
// M1 deliberately does NOT build that mechanism (the M1 brief permits deferring
// it: host replay is single-threaded). What it builds is the *seam* — this API —
// so that M3 replaces the internals with a seqlock/double buffer and no caller
// changes. What the API already forbids is the design mistake: there is no way
// for the consumer to make the producer wait, no way to hand out a reference
// into producer storage, and no way to observe a half-written frame (consume()
// copies a complete one or reports there is nothing new).
//
// M1 storage: one slot, one version counter. Single-threaded use only — the
// class is not a synchronisation primitive yet and does not pretend to be.
#pragma once

#include <cstdint>

#include "depthcharge/display_snapshot.hpp"

namespace depthcharge {

class SnapshotChannel {
public:
    // Feed side. Wait-free by contract — it must never block on the consumer.
    void publish(const DisplaySnapshot& snap) noexcept {
        slot_ = snap;
        published_ = snap.version;
    }

    // Render side. Copies the latest complete frame into `out` and returns true;
    // returns false if nothing has been published since the last consume, so a
    // render task can skip redrawing rather than spin.
    bool consume(DisplaySnapshot& out) noexcept {
        if (published_ == consumed_) { return false; }
        out = slot_;
        consumed_ = published_;
        return true;
    }

    // Latest published version without consuming — for stats and tests.
    std::uint32_t published_version() const noexcept { return published_; }
    std::uint32_t consumed_version() const noexcept { return consumed_; }

private:
    DisplaySnapshot slot_{};
    std::uint32_t published_ = 0;
    std::uint32_t consumed_ = 0;
};

}  // namespace depthcharge
