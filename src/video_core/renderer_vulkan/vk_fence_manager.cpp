// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "video_core/renderer_vulkan/vk_buffer_cache.h"
#include "video_core/renderer_vulkan/vk_fence_manager.h"
#include "video_core/renderer_vulkan/vk_query_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/trudy_counters.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

InnerFence::InnerFence(Scheduler& scheduler_, bool is_stubbed_)
    : FenceBase{is_stubbed_}, scheduler{scheduler_} {}

InnerFence::~InnerFence() = default;

void InnerFence::Queue(VideoCore::RasterizerInterface& rasterizer) {
    if (is_stubbed) {
        return;
    }
    // Get the current tick so we can wait for it
    wait_tick = scheduler.CurrentTick();
    if (Scheduler::BatchesFenceSubmits()) {
        // Trudy: the tick is submitted with the next batch (Scheduler::RequestDeferredFlush).
        VideoCommon::TrudyCounters::Add(VideoCommon::TrudyCounters::Event::SubmitFence);
        scheduler.RequestDeferredFlush();
        return;
    }
    rasterizer.FlushCommands();
    if (scheduler.CurrentTick() == wait_tick) {
        VideoCommon::TrudyCounters::Add(VideoCommon::TrudyCounters::Event::SubmitFence);
        scheduler.Flush();
    }
}

bool InnerFence::IsSignaled() const {
    if (is_stubbed) {
        return true;
    }
    return scheduler.IsFree(wait_tick);
}

void InnerFence::Wait() {
    if (is_stubbed) {
        return;
    }
    // Trudy: with batched submissions the tick may not be submitted yet. Only the recording
    // thread may submit it (Scheduler::Wait flushes); the fencing thread waits for the host GPU
    // instead, and the recording thread submits deferred work before it goes idle.
    if (Scheduler::BatchesFenceSubmits() && !scheduler.IsRecordingThread()) {
        if (!scheduler.IsFree(wait_tick)) {
            scheduler.GetMasterSemaphore().Wait(wait_tick);
        }
        return;
    }
    scheduler.Wait(wait_tick);
}

FenceManager::FenceManager(VideoCore::RasterizerInterface& rasterizer_, Tegra::GPU& gpu_,
                           TextureCache& texture_cache_, BufferCache& buffer_cache_,
                           QueryCache& query_cache_, const Device& device_, Scheduler& scheduler_)
    : GenericFenceManager{rasterizer_, gpu_, texture_cache_, buffer_cache_, query_cache_},
      scheduler{scheduler_} {}

Fence FenceManager::CreateFence(bool is_stubbed) {
    return std::make_shared<InnerFence>(scheduler, is_stubbed);
}

void FenceManager::QueueFence(Fence& fence) {
    fence->Queue(rasterizer);
}

bool FenceManager::IsFenceSignaled(Fence& fence) const {
    return fence->IsSignaled();
}

void FenceManager::WaitFence(Fence& fence) {
    fence->Wait();
}

} // namespace Vulkan
