// SPDX-FileCopyrightText: Copyright 2026 Trudy contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/cache_affinity.h"

#include <cstdlib>
#include <string_view>

#include "common/common_types.h"
#include "common/logging.h"

#if defined(_WIN32)
#include <vector>
#include <windows.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

namespace Common {

namespace {

bool AffinityDisabled() {
    const char* value = std::getenv("TRUDY_CCD_AFFINITY");
    return value != nullptr && std::string_view{value} == "0";
}

#if defined(_WIN32)

struct FastDomain {
    bool valid = false;
    GROUP_AFFINITY mask{};
};

FastDomain FindFastDomain() {
    FastDomain result{};
    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationCache, nullptr, &length);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || length == 0) {
        return result;
    }
    std::vector<u8> buffer(length);
    auto* const base = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
    if (!GetLogicalProcessorInformationEx(RelationCache, base, &length)) {
        return result;
    }

    u32 domains = 0;
    DWORD best_size = 0;
    for (DWORD offset = 0; offset < length;) {
        const auto* const info =
            reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (info->Relationship == RelationCache && info->Cache.Level == 3 &&
            (info->Cache.Type == CacheUnified || info->Cache.Type == CacheData)) {
            ++domains;
            if (info->Cache.CacheSize > best_size) {
                best_size = info->Cache.CacheSize;
                result.mask = info->Cache.GroupMask;
            }
        }
        offset += info->Size;
    }

    if (domains < 2) {
        LOG_INFO(Common, "Trudy: one L3 cache domain, thread placement left to the OS");
        return result;
    }
    // Only restrict when the domain is inside the process's own affinity.
    DWORD_PTR process_mask = 0, system_mask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) &&
        result.mask.Group == 0 && (result.mask.Mask & process_mask) == 0) {
        LOG_WARNING(Common, "Trudy: fastest L3 domain is outside the process affinity, not pinning");
        return result;
    }
    result.valid = true;
    LOG_INFO(Common,
             "Trudy: {} L3 cache domains, hot threads kept on the {} MiB one (group {}, mask {:#x})",
             domains, best_size >> 20, result.mask.Group, static_cast<u64>(result.mask.Mask));
    return result;
}

void Apply(const FastDomain& domain) {
    if (!domain.valid) {
        return;
    }
    GROUP_AFFINITY mask = domain.mask;
    if (!SetThreadGroupAffinity(GetCurrentThread(), &mask, nullptr)) {
        LOG_WARNING(Common, "Trudy: SetThreadGroupAffinity failed ({})", GetLastError());
    }
}

#elif defined(__linux__) && !defined(__ANDROID__)

struct FastDomain {
    bool valid = false;
    cpu_set_t set{};
};

// "0-7,16-23" -> {0..7, 16..23}
std::set<int> ParseCpuList(const std::string& text) {
    std::set<int> cpus;
    std::stringstream stream{text};
    std::string part;
    while (std::getline(stream, part, ',')) {
        const auto dash = part.find('-');
        try {
            if (dash == std::string::npos) {
                cpus.insert(std::stoi(part));
            } else {
                const int first = std::stoi(part.substr(0, dash));
                const int last = std::stoi(part.substr(dash + 1));
                for (int cpu = first; cpu <= last; ++cpu) {
                    cpus.insert(cpu);
                }
            }
        } catch (...) {
        }
    }
    return cpus;
}

// "98304K" -> bytes
u64 ParseCacheSize(std::string text) {
    u64 multiplier = 1;
    if (!text.empty() && (text.back() == 'K' || text.back() == 'k')) {
        multiplier = 1024;
        text.pop_back();
    } else if (!text.empty() && (text.back() == 'M' || text.back() == 'm')) {
        multiplier = 1024 * 1024;
        text.pop_back();
    }
    try {
        return std::stoull(text) * multiplier;
    } catch (...) {
        return 0;
    }
}

FastDomain FindFastDomain() {
    FastDomain result{};
    const long count = sysconf(_SC_NPROCESSORS_CONF);
    std::map<std::string, u64> domains; // shared_cpu_list -> size
    for (long cpu = 0; cpu < count; ++cpu) {
        const std::string dir =
            "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cache/index3/";
        std::ifstream list_file{dir + "shared_cpu_list"};
        std::ifstream size_file{dir + "size"};
        std::string list, size;
        if (!std::getline(list_file, list) || !std::getline(size_file, size)) {
            continue;
        }
        domains[list] = ParseCacheSize(size);
    }
    if (domains.size() < 2) {
        LOG_INFO(Common, "Trudy: one L3 cache domain, thread placement left to the OS");
        return result;
    }

    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
        return result;
    }
    std::string best_list;
    u64 best_size = 0;
    for (const auto& [list, size] : domains) {
        if (size > best_size) {
            best_size = size;
            best_list = list;
        }
    }
    CPU_ZERO(&result.set);
    for (const int cpu : ParseCpuList(best_list)) {
        if (cpu >= 0 && cpu < CPU_SETSIZE && CPU_ISSET(cpu, &allowed)) {
            CPU_SET(cpu, &result.set);
        }
    }
    if (CPU_COUNT(&result.set) == 0) {
        return result;
    }
    result.valid = true;
    LOG_INFO(Common, "Trudy: {} L3 cache domains, hot threads kept on the {} MiB one (CPUs {})",
             domains.size(), best_size >> 20, best_list);
    return result;
}

void Apply(const FastDomain& domain) {
    if (!domain.valid) {
        return;
    }
    if (pthread_setaffinity_np(pthread_self(), sizeof(domain.set), &domain.set) != 0) {
        LOG_WARNING(Common, "Trudy: pthread_setaffinity_np failed");
    }
}

#endif

} // Anonymous namespace

void PinCurrentThreadToFastCacheDomain() {
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
    if (AffinityDisabled()) {
        return;
    }
    static const FastDomain domain = FindFastDomain();
    Apply(domain);
#endif
}

} // namespace Common
