// SPDX-FileCopyrightText: Copyright 2026 Trudy contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <deque>
#include <string>
#include <unordered_map>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class Scheduler;

/// Trudy: TRUDY_GPU_TIMING=1 measures how long the host GPU spends on each compute shader, on
/// each graphics pipeline's draws and on each render pass, with Vulkan timestamps, and logs the
/// biggest every 10 s as milliseconds per second of wall time. Profiling only: the timestamps
/// cost GPU time themselves, and timestamps of overlapping draws overlap.
///
/// Begin/End/Collect run on the thread that records (the GPU thread, or the split's back
/// thread); the timestamp writes run where the scheduler executes its commands.
class GpuTimer {
public:
    /// Submit: a whole main command buffer; Upload: a whole upload command buffer (both run
    /// once per queue submission). The others are parts of the main command buffer.
    enum class Kind : u8 { Compute, Draw, Pass, Submit, Upload };
    static constexpr u32 Invalid = ~0u;

    static bool Enabled();
    /// The timer, or nullptr when timing is off or the device cannot do it.
    static GpuTimer* Get() noexcept {
        return instance;
    }
    static void Create(const Device& device, Scheduler& scheduler);
    static void Destroy();

    u32 Begin(Kind kind, u64 key);
    void End(u32 token);

    /// TRUDY_GPU_TRACE: reserves a timestamp for the command just recorded into the command
    /// buffer of `tick`; ~0u when none is free. The caller records the write into MarkPool().
    u32 AllocateMark(const char* label, u64 tick);
    [[nodiscard]] VkQueryPool MarkPool() const noexcept {
        return *mark_pool;
    }
    /// Reads finished timestamps; logs every 10 s.
    void Collect();

private:
    GpuTimer(const Device& device, Scheduler& scheduler);

    struct Pending {
        u32 pair;
        u64 tick;
        u64 key;
        Kind kind;
        bool ended;
    };
    struct Total {
        u64 ns;
        u64 count;
        Kind kind;
    };

    static constexpr u32 Pairs = 16384;

    static inline GpuTimer* instance = nullptr;

    const Device& device;
    Scheduler& scheduler;
    vk::QueryPool pool;
    double ns_per_tick;
    u32 next_pair = 0;
    std::deque<Pending> pending;
    std::unordered_map<u64, Total> totals; ///< by (kind, key)
    std::unordered_map<u32, bool> upload_pairs; ///< pairs written into the upload buffer

    struct Mark {
        u32 query;
        u64 tick;
        const char* label;
    };
    static constexpr u32 Marks = 65536;
    vk::QueryPool mark_pool;
    u32 next_mark = 0;
    std::deque<Mark> marks;
    u64 last_mark_tick = ~0ULL;
    u64 last_mark_stamp = 0;
    std::unordered_map<const char*, Total> mark_totals;
    u64 window_start_ns = 0;
    u64 frames_dropped = 0;
};

} // namespace Vulkan
