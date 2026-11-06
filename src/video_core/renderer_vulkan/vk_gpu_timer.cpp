// SPDX-FileCopyrightText: Copyright 2026 Trudy contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/renderer_vulkan/vk_gpu_timer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "common/logging.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/trudy_counters.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

namespace {
u64 NowNs() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
}

const char* KindName(GpuTimer::Kind kind) {
    switch (kind) {
    case GpuTimer::Kind::Compute:
        return "compute";
    case GpuTimer::Kind::Draw:
        return "draws";
    case GpuTimer::Kind::Pass:
        return "pass";
    case GpuTimer::Kind::Submit:
        return "cmdbuf";
    case GpuTimer::Kind::Upload:
        return "upload";
    }
    return "?";
}
} // Anonymous namespace

bool GpuTimer::Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("TRUDY_GPU_TIMING");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

void GpuTimer::Create(const Device& device, Scheduler& scheduler) {
    if (!Enabled() || instance != nullptr) {
        return;
    }
    if (!device.IsHostQueryResetSupported() || device.GetTimestampPeriod() <= 0.0f) {
        LOG_WARNING(Render_Vulkan, "Trudy GPU timing: the device lacks host query reset or "
                                   "timestamps, timing off");
        return;
    }
    instance = new GpuTimer(device, scheduler);
    VideoCommon::TrudyCounters::enabled = true;
    LOG_INFO(Render_Vulkan, "Trudy GPU timing on ({:.2f} ns per timestamp tick)",
             instance->ns_per_tick);
}

void GpuTimer::Destroy() {
    delete instance;
    instance = nullptr;
}

GpuTimer::GpuTimer(const Device& device_, Scheduler& scheduler_)
    : device{device_}, scheduler{scheduler_},
      ns_per_tick{static_cast<double>(device_.GetTimestampPeriod())} {
    pool = device.GetLogical().CreateQueryPool({
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = Pairs * 2,
        .pipelineStatistics = 0,
    });
    device.GetLogical().ResetQueryPool(*pool, 0, Pairs * 2);
    mark_pool = device.GetLogical().CreateQueryPool({
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = Marks,
        .pipelineStatistics = 0,
    });
    device.GetLogical().ResetQueryPool(*mark_pool, 0, Marks);
    window_start_ns = NowNs();
}

u32 GpuTimer::AllocateMark(const char* label, u64 tick) {
    if (marks.size() >= Marks) {
        return Invalid;
    }
    const u32 query = next_mark;
    next_mark = (next_mark + 1) % Marks;
    marks.push_back({query, tick, label});
    return query;
}

u32 GpuTimer::Begin(Kind kind, u64 key) {
    if (pending.size() >= Pairs) {
        ++frames_dropped; // the GPU is too far behind; skip rather than reuse a live pair
        return Invalid;
    }
    const u32 pair = next_pair;
    next_pair = (next_pair + 1) % Pairs;
    pending.push_back({pair, scheduler.CurrentTick(), key, kind, false});
    if (kind == Kind::Upload) {
        upload_pairs[pair] = true;
        scheduler.RecordWithUploadBuffer(
            [pool = *pool, pair](vk::CommandBuffer, vk::CommandBuffer upload_cmdbuf) {
                upload_cmdbuf.WriteTimestamp(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, pair * 2);
            });
        return pair;
    }
    scheduler.Record([pool = *pool, pair](vk::CommandBuffer cmdbuf) {
        cmdbuf.WriteTimestamp(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, pair * 2);
    });
    return pair;
}

void GpuTimer::End(u32 token) {
    if (token == Invalid) {
        return;
    }
    // Usually the newest entry; search back in case of nesting (draws inside a pass).
    for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
        if (it->pair == token) {
            it->ended = true;
            it->tick = scheduler.CurrentTick();
            break;
        }
    }
    if (upload_pairs.erase(token) != 0) {
        scheduler.RecordWithUploadBuffer(
            [pool = *pool, token](vk::CommandBuffer, vk::CommandBuffer upload_cmdbuf) {
                upload_cmdbuf.WriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool,
                                             token * 2 + 1);
            });
        return;
    }
    scheduler.Record([pool = *pool, token](vk::CommandBuffer cmdbuf) {
        cmdbuf.WriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, token * 2 + 1);
    });
}

void GpuTimer::Collect() {
    const auto& logical = device.GetLogical();
    while (!pending.empty()) {
        const Pending& front = pending.front();
        if (!front.ended || !scheduler.IsFree(front.tick)) {
            break;
        }
        std::array<u64, 2> stamps{};
        const VkResult result =
            logical.GetQueryResults(*pool, front.pair * 2, 2, sizeof(stamps), stamps.data(),
                                    sizeof(u64), VK_QUERY_RESULT_64_BIT);
        if (result == VK_NOT_READY) {
            break;
        }
        if (result == VK_SUCCESS && stamps[1] >= stamps[0]) {
            const u64 ns = static_cast<u64>(static_cast<double>(stamps[1] - stamps[0]) * ns_per_tick);
            const u64 slot = (front.key * 4) ^ static_cast<u64>(front.kind);
            Total& total = totals[slot];
            total.ns += ns;
            ++total.count;
            total.kind = front.kind;
        }
        logical.ResetQueryPool(*pool, front.pair * 2, 2);
        pending.pop_front();
    }

    // TRUDY_GPU_TRACE marks: the time from one command's completion to the next is the
    // next command's (its waits and barriers included), within one command buffer.
    while (!marks.empty()) {
        const Mark& mark = marks.front();
        if (!scheduler.IsFree(mark.tick)) {
            break;
        }
        u64 stamp{};
        const VkResult result = logical.GetQueryResults(*mark_pool, mark.query, 1, sizeof(stamp),
                                                        &stamp, sizeof(u64),
                                                        VK_QUERY_RESULT_64_BIT);
        if (result == VK_NOT_READY) {
            break;
        }
        if (result == VK_SUCCESS) {
            if (mark.tick == last_mark_tick && stamp >= last_mark_stamp) {
                Total& total = mark_totals[mark.label];
                total.ns += static_cast<u64>(static_cast<double>(stamp - last_mark_stamp) *
                                             ns_per_tick);
                ++total.count;
            }
            last_mark_tick = mark.tick;
            last_mark_stamp = stamp;
        }
        logical.ResetQueryPool(*mark_pool, mark.query, 1);
        marks.pop_front();
    }

    const u64 now = NowNs();
    const u64 window = now - window_start_ns;
    if (window < 10'000'000'000ULL) {
        return;
    }
    if (!mark_totals.empty()) {
        // MSVC: "... TraceLabel<class `Vulkan::X::F'::`2'::<lambda_1>>(void)" -> Vulkan::X::F
        // GCC/Clang: "... [T = (lambda at C:/.../vk_x.cpp:123:4)]" -> vk_x.cpp:123
        const auto short_name = [](std::string_view name) {
            if (const size_t at = name.find("lambda at "); at != name.npos) {
                std::string_view where = name.substr(at + 10);
                where = where.substr(0, where.find(')'));
                if (const size_t slash = where.find_last_of("/\\"); slash != where.npos) {
                    where = where.substr(slash + 1);
                }
                if (const size_t column = where.rfind(':'); column != where.npos &&
                                                            where.find(':') != column) {
                    where = where.substr(0, column);
                }
                return std::string{where};
            }
            std::string_view rest = name;
            if (const size_t record = rest.find("Record<"); record != rest.npos) {
                rest = rest.substr(record + 7);
            }
            const size_t open = rest.find('`');
            if (open == rest.npos) {
                return std::string{name.substr(0, std::min<size_t>(name.size(), 120))};
            }
            const size_t close = rest.find('\'', open + 1);
            return std::string{rest.substr(open + 1, (close == rest.npos ? rest.size() : close) -
                                                         open - 1)};
        };
        std::unordered_map<std::string, Total> by_name;
        u64 traced{};
        for (const auto& [label, total] : mark_totals) {
            Total& merged = by_name[short_name(label)];
            merged.ns += total.ns;
            merged.count += total.count;
            traced += total.ns;
        }
        std::vector<std::pair<std::string, Total>> sorted(by_name.begin(), by_name.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.second.ns > b.second.ns; });
        const double scale = 1e9 / static_cast<double>(window) / 1e6;
        LOG_INFO(Render_Vulkan, "Trudy GPU trace: {:.1f} ms/s between traced commands",
                 static_cast<double>(traced) * scale);
        for (size_t i = 0; i < std::min<size_t>(sorted.size(), 30); ++i) {
            LOG_INFO(Render_Vulkan, "Trudy trace {:>8.2f} ms/s {:>8.0f}/s  {}",
                     static_cast<double>(sorted[i].second.ns) * scale,
                     static_cast<double>(sorted[i].second.count) * 1e9 / static_cast<double>(window),
                     sorted[i].first);
        }
        mark_totals.clear();
    }
    struct Row {
        u64 slot;
        Total total;
    };
    std::vector<Row> rows;
    rows.reserve(totals.size());
    std::array<u64, 5> by_kind{};
    for (const auto& [slot, total] : totals) {
        rows.push_back({slot, total});
        by_kind[static_cast<size_t>(total.kind)] += total.ns;
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) { return a.total.ns > b.total.ns; });
    const double per_second = 1e9 / static_cast<double>(window);
    const auto ms = [&](Kind kind) {
        return static_cast<double>(by_kind[static_cast<size_t>(kind)]) * per_second / 1e6;
    };
    std::string line = fmt::format(
        "GPU time per second: main command buffers {:.1f} ms, upload command buffers {:.1f} ms; "
        "inside them: render passes {:.1f} ms (draws {:.1f} ms), compute {:.1f} ms, "
        "everything else {:.1f} ms",
        ms(Kind::Submit), ms(Kind::Upload), ms(Kind::Pass), ms(Kind::Draw), ms(Kind::Compute),
        ms(Kind::Submit) - ms(Kind::Pass) - ms(Kind::Compute));
    if (frames_dropped != 0) {
        line += fmt::format(" ({} timings skipped)", frames_dropped);
    }
    LOG_INFO(Render_Vulkan, "Trudy {}", line);
    {
        using VideoCommon::TrudyCounters::Event;
        std::array<u64, static_cast<size_t>(Event::Count)> c{};
        for (size_t i = 0; i < c.size(); ++i) {
            c[i] = VideoCommon::TrudyCounters::counts[i].exchange(0, std::memory_order_relaxed);
        }
        const auto rate = [&](Event e) {
            return static_cast<double>(c[static_cast<size_t>(e)]) * per_second;
        };
        LOG_INFO(Render_Vulkan,
                 "Trudy per second: submits {:.0f} (fences {:.0f}, waits {:.0f}); fences skipped "
                 "{:.0f}, stubbed {:.0f}, with texture downloads {:.0f}, buffer downloads {:.0f}, "
                 "query results {:.0f}",
                 rate(Event::SubmitOther), rate(Event::SubmitFence), rate(Event::SubmitWait),
                 rate(Event::FenceSkipped), rate(Event::FenceStubbed),
                 rate(Event::FenceTextureFlush), rate(Event::FenceBufferFlush),
                 rate(Event::FenceQueryFlush));
        LOG_INFO(Render_Vulkan,
                 "Trudy per second: buffer copies reordered {:.0f}, in order {:.0f} ({:.0f} right "
                 "after another, {:.0f} KiB)",
                 rate(Event::CopyReordered), rate(Event::CopyInOrder),
                 rate(Event::CopyInOrderAdjacent), rate(Event::CopyInOrderBytes) / 1024.0);
    }
    const size_t shown = std::min<size_t>(rows.size(), 25);
    for (size_t i = 0; i < shown; ++i) {
        const Row& row = rows[i];
        LOG_INFO(Render_Vulkan, "Trudy GPU  {:>8.2f} ms/s {:>8.0f}/s  {:<7} {:016x}",
                 static_cast<double>(row.total.ns) * per_second / 1e6,
                 static_cast<double>(row.total.count) * per_second, KindName(row.total.kind),
                 row.slot / 4);
    }
    totals.clear();
    frames_dropped = 0;
    window_start_ns = now;
}

} // namespace Vulkan
