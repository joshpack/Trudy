// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/game_settings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

#include "common/game_presets.h"
#include "common/logging.h"
#include "common/settings.h"
#include "video_core/renderer_base.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace Core::GameSettings {

static GPUVendor GetGPU(const std::string& gpu_vendor_string) {
    struct Entry { const char* name; GPUVendor vendor; };
    static constexpr Entry GpuVendor[] = {
        // NVIDIA
        {"NVIDIA",   GPUVendor::Nvidia},
        {"Nouveau",  GPUVendor::Nvidia},
        {"NVK",      GPUVendor::Nvidia},
        {"Tegra",    GPUVendor::Nvidia},
        // AMD
        {"AMD",       GPUVendor::AMD},
        {"RadeonSI",  GPUVendor::AMD},
        {"RADV",      GPUVendor::AMD},
        {"AMDVLK",    GPUVendor::AMD},
        {"R600",      GPUVendor::AMD},
        // Intel
        {"Intel",     GPUVendor::Intel},
        {"ANV",       GPUVendor::Intel},
        {"i965",      GPUVendor::Intel},
        {"i915",      GPUVendor::Intel},
        {"OpenSWR",   GPUVendor::Intel},
        // Apple
        {"Apple",     GPUVendor::Apple},
        {"MoltenVK",  GPUVendor::Apple},
        // Qualcomm / Adreno
        {"Qualcomm",  GPUVendor::Qualcomm},
        {"Turnip",    GPUVendor::Qualcomm},
        // ARM / Mali
        {"Mali",      GPUVendor::ARM},
        {"PanVK",     GPUVendor::ARM},
        // Imagination / PowerVR
        {"PowerVR",   GPUVendor::Imagination},
        {"PVR",       GPUVendor::Imagination},
        // Microsoft / WARP / D3D12 GL
        {"D3D12",     GPUVendor::Microsoft},
        {"Microsoft", GPUVendor::Microsoft},
        {"WARP",      GPUVendor::Microsoft},
    };

    for (const auto& entry : GpuVendor) {
        if (gpu_vendor_string == entry.name) {
            return entry.vendor;
        }
    }

    // legacy (shouldn't be needed anymore, but just in case)
    std::string gpu = gpu_vendor_string;
    std::transform(gpu.begin(), gpu.end(), gpu.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    if (gpu.find("geforce") != std::string::npos) {
        return GPUVendor::Nvidia;
    }
    if (gpu.find("radeon") != std::string::npos || gpu.find("ati") != std::string::npos) {
        return GPUVendor::AMD;
    }

    return GPUVendor::Unknown;
}

static OS DetectOS() {
#if defined(_WIN32)
    return OS::Windows;
#elif defined(__FIREOS__)
    return OS::FireOS;
#elif defined(__ANDROID__)
    return OS::Android;
#elif defined(__OHOS__)
    return OS::HarmonyOS;
#elif defined(__HAIKU__)
    return OS::HaikuOS;
#elif defined(__DragonFly__)
    return OS::DragonFlyBSD;
#elif defined(__NetBSD__)
    return OS::NetBSD;
#elif defined(__OpenBSD__)
    return OS::OpenBSD;
#elif defined(_AIX)
    return OS::AIX;
#elif defined(__managarm__)
    return OS::Managarm;
#elif defined(__redox__)
    return OS::RedoxOS;
#elif defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    return OS::IOS;
#elif defined(__APPLE__)
    return OS::MacOS;
#elif defined(__FreeBSD__)
    return OS::FreeBSD;
#elif defined(__sun) && defined(__SVR4)
    return OS::Solaris;
#elif defined(__linux__)
    return OS::Linux;
#else
    return OS::Unknown;
#endif
}

EnvironmentInfo DetectEnvironment(const VideoCore::RendererBase& renderer) {
    EnvironmentInfo env{};
    env.os = DetectOS();
    env.vendor_string = renderer.GetDeviceVendor();
    env.vendor = GetGPU(env.vendor_string);
    return env;
}

// A title default only where the user kept the global value; set as the per-game value, which
// RestoreGlobalState drops again when emulation stops (the global config is never touched).
template <typename T, bool ranged>
static void OverrideIfGlobal(Settings::SwitchableSetting<T, ranged>& setting, T value) {
    if (!setting.UsingGlobal()) {
        return;
    }
    setting.SetGlobal(false);
    setting.SetValue(value);
}

// Trudy: whether the running title is a Switch 2 program. Set at every boot by LoadEarlyOverrides;
// LoadOverrides runs again after the configuration dialog and needs it then too.
static bool g_switch2_title = false;

// Trudy: a default only where the user changed neither the game's own value nor the global one
// (OverrideIfGlobal keeps only the per-game choice; a global value the user picked wins here).
template <typename T, bool ranged>
static bool OverrideIfUntouched(Settings::SwitchableSetting<T, ranged>& setting, T value) {
    if (!setting.UsingGlobal() || setting.GetValue(true) != setting.GetDefault() ||
        setting.GetDefault() == value) {
        return false;
    }
    setting.SetGlobal(false);
    setting.SetValue(value);
    return true;
}

// Trudy: the settings every one of Orchid's Switch 2 presets shares (common/game_presets.h), for
// the Switch 2 titles that have no preset of their own. GPU accuracy Low lets the GPU thread split
// run (NX2_NOTES row 145: 1.46x draws a second; High keeps it off), with Balanced fences, which
// Mario Tennis Fever and Hyrule Warriors needed under Low (occlusion reports stay in the query
// cache, or conditional rendering hides objects), and asynchronous shaders. Titles with a preset
// keep the boot prompt's choice. TRUDY_SWITCH2_DEFAULTS=0 turns this off.
static void ApplySwitch2Defaults(std::uint64_t program_id) {
    if (!g_switch2_title || Settings::FindGamePreset(program_id) != nullptr) {
        return;
    }
    if (const char* value = std::getenv("TRUDY_SWITCH2_DEFAULTS"); value != nullptr && value[0] == '0') {
        return;
    }
    std::string applied;
    if (OverrideIfUntouched(Settings::values.gpu_accuracy, Settings::GpuAccuracy::Low)) {
        applied += " GPU accuracy Low,";
        // Balanced fences only make sense with the accuracy they were chosen for.
        if (OverrideIfUntouched(Settings::values.gpu_fence_behavior,
                                Settings::GpuFenceBehavior::Balanced)) {
            applied += " Balanced fences,";
        }
    }
    if (OverrideIfUntouched(Settings::values.use_asynchronous_shaders, true)) {
        applied += " asynchronous shaders,";
    }
    if (!applied.empty()) {
        applied.pop_back();
        LOG_INFO(Core, "Trudy: Switch 2 defaults for {:016X}:{} (TRUDY_SWITCH2_DEFAULTS=0 to skip)",
                 program_id, applied);
    }
}

void LoadEarlyOverrides(std::uint64_t program_id, bool is_switch2) {
    g_switch2_title = is_switch2;
    switch (static_cast<TitleID>(program_id)) {
        case TitleID::MarioTennisFever:
            // Present from the present thread: otherwise the GPU thread's back half acquires the
            // swapchain image and presents itself, and with FIFO vsync each can block it for up to
            // a refresh while the next frame's draws wait. The present thread's crashes (NX2_NOTES
            // row 118) were fixed before row 138; it has been on in the Switch 2 dev config since.
            OverrideIfGlobal(Settings::values.async_presentation, true);
            break;
        default:
            break;
    }
}

void LoadOverrides(std::uint64_t program_id, const VideoCore::RendererBase& renderer) {
    const auto env = DetectEnvironment(renderer);
    Settings::values.preemptive_cpu_reads = false;
    ApplySwitch2Defaults(program_id);

    switch (static_cast<TitleID>(program_id)) {
        case TitleID::NinjaGaidenRagebound:
            Settings::values.use_squashed_iterated_blend = true;
            break;
        case TitleID::MarioTennisFever:
            // Low accuracy lets the GPU thread split (30 -> 60 fps in a match); Balanced fences keep
            // occlusion reports in the query cache, or its conditional rendering hides the characters.
            OverrideIfGlobal(Settings::values.gpu_accuracy, Settings::GpuAccuracy::Low);
            OverrideIfGlobal(Settings::values.gpu_fence_behavior, Settings::GpuFenceBehavior::Balanced);
            // Reactive flushing takes read permission off every page the GPU caches, so each guest
            // CPU read of one takes the slow path (~12% of the main core in title 1, NX2_NOTES row
            // 115). Switch 2 titles have read traps instead (row 150), which protect only pages the
            // GPU wrote, once each, and which only run with reactive flushing off.
            OverrideIfGlobal(Settings::values.use_reactive_flushing, false);
            break;
        case TitleID::HyruleWarriorsAgeOfImprisonment:
            // Game time follows the guest counter, which a faster CPU clock scales (Boost ran the
            // game at 1.7x speed); low accuracy for the GPU thread split. Balanced fences signal
            // syncpoints once the host GPU is done: earlier, the game reads GPU-written buffers
            // right away and each read waits for the whole host GPU queue.
            OverrideIfGlobal(Settings::values.cpu_clock, Settings::CpuClock::Normal);
            OverrideIfGlobal(Settings::values.gpu_accuracy, Settings::GpuAccuracy::Low);
            OverrideIfGlobal(Settings::values.gpu_fence_behavior, Settings::GpuFenceBehavior::Balanced);
            // The game reads GPU-written buffer pages every frame; each ordered read waited for the
            // whole host GPU queue (about 7 ms, 45 instead of 60 fps in combat).
            Settings::values.preemptive_cpu_reads = true;
            break;
        case TitleID::DonkeyKongBananza:
            // Same per-frame reads of GPU-written buffers: each ordered read waited for the host
            // GPU, the game measured the stall as frame time and dropped to swap interval 2-3
            // (about 22 instead of 59 presents a second in the first area, RADV, docked 2x).
            Settings::values.preemptive_cpu_reads = true;
            // GPU accuracy High kept the GPU thread split off; fps drops below 30 then slowed the
            // game itself down. Low + no reactive flushing (as Mario Tennis Fever) keeps it at speed.
            OverrideIfGlobal(Settings::values.gpu_accuracy, Settings::GpuAccuracy::Low);
            OverrideIfGlobal(Settings::values.use_reactive_flushing, false);
            // The game times its GPU work with query timestamps: at the Normal GPU clock it saw
            // the emulator's frame time, lowered its resolution to 960x540 and held swap interval
            // 2-3 (~25 fps). Its game step follows the interval it requests (held to 2 vblanks
            // while it asked for 1 it ran at half speed), so the clock only changes which it picks.
            OverrideIfGlobal(Settings::values.gpu_clock, Settings::GpuClock::Boost);
            break;
        default:
            break;
    }

    LOG_INFO(Core, "Applied game settings for title ID {:016X} on OS {}, GPU vendor {} ({})",
             program_id,
             static_cast<int>(env.os),
             static_cast<int>(env.vendor),
             env.vendor_string);
}

} // namespace Core::GameSettings
