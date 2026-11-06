// SPDX-FileCopyrightText: Copyright 2026 Trudy contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>

#include "common/common_types.h"

/// Trudy: event counters for TRUDY_GPU_TIMING=1 (logged with the GPU times every 10 s by
/// Vulkan::GpuTimer). Counting costs one relaxed atomic add; nothing is counted unless timing is on.
namespace VideoCommon::TrudyCounters {

enum class Event : u32 {
    FenceSkipped,       ///< SignalFence returned without a fence
    FenceStubbed,       ///< a fence with nothing to wait for (no submit)
    FenceTextureFlush,  ///< fence made for texture downloads
    FenceBufferFlush,   ///< fence made for buffer downloads
    FenceQueryFlush,    ///< fence made for query results
    FenceForced64,      ///< the every-64th fence SkipEmptyFence keeps
    SubmitFence,        ///< a queue submission made by a fence
    SubmitWait,         ///< a queue submission made to wait for a tick
    SubmitOther,        ///< every other queue submission
    CopyReordered,      ///< buffer copy moved to the upload command buffer (no barriers)
    CopyInOrder,        ///< buffer copy in the main command buffer with full barriers
    CopyInOrderAdjacent,///< of CopyInOrder: the previous recorded command was such a copy too
    CopyInOrderBytes,   ///< bytes copied in order
    Count,
};

inline std::array<std::atomic<u64>, static_cast<size_t>(Event::Count)> counts{};
inline std::atomic<bool> enabled{false};

inline void Add(Event event, u64 amount = 1) {
    if (enabled.load(std::memory_order_relaxed)) {
        counts[static_cast<size_t>(event)].fetch_add(amount, std::memory_order_relaxed);
    }
}

} // namespace VideoCommon::TrudyCounters
