// SPDX-FileCopyrightText: Copyright 2026 Trudy contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace Common {

/// Trudy: keeps the calling thread on the CPUs that share the fastest L3 cache.
///
/// On processors with more than one L3 domain (AMD Ryzen parts with two CCDs, such as the
/// 7950X3D / 9950X3D / 9900X3D / 9950X), the emulator's hot threads (guest CPU cores, the GPU
/// front and back threads, the Vulkan worker) talk to each other constantly. Keeping them on one
/// CCD avoids the ~70-80 ns cross-CCD hops, and on X3D parts it puts them on the CCD with the
/// stacked 3D V-Cache, which the macro memo and the caches' hash maps benefit from.
///
/// The largest L3 wins; on a tie the first domain is used. With a single L3 domain (9800X3D,
/// 7800X3D, any 8-core part, most Intel CPUs) nothing changes. TRUDY_CCD_AFFINITY=0 turns it off.
void PinCurrentThreadToFastCacheDomain();

} // namespace Common
