<!--
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->
<!-- lang: en-GB -->

<h1 align="center">
  <br>
  <img src="dist/Trudy.png" alt="Trudy" width="200">
  <br>
  <b>Trudy</b>
  <br>
</h1>

<h4 align="center">An experimental Switch 2 emulator tuned for AMD Ryzen CPUs and NVIDIA RTX 40 / RTX 50
graphics cards. Trudy is a fork of <a href="https://github.com/MythosOfs/Orchid">Orchid</a> and keeps
all of Orchid's game fixes; it adds hardware-specific defaults and builds on top.</h4>

> [!NOTE]
> Trudy is an independent fork. Please report problems with Trudy builds here, not to the Orchid,
> Eglantine or Eden developers.

<p align="center">
  <a href="#why-trudy-instead-of-orchid">Why Trudy</a> |
  <a href="#what-trudy-changes">Changes</a> |
  <a href="#test-results">Test results</a> |
  <a href="#settings">Settings</a> |
  <a href="#download">Download</a> |
  <a href="#build-it-yourself">Build it yourself</a> |
  <a href="#credits">Credits</a>
</p>

## Why Trudy instead of Orchid

Orchid's own profiling shows that Switch 2 emulation is limited by **one host CPU thread**, the one
that turns the game's GPU commands into Vulkan work. In Mario Kart World that thread runs at
~100% while the host GPU sits at 20-40%. So the CPU, its caches and how the hot threads are placed
matter most. Trudy works on those, and makes better use of what RTX 40/50 cards offer:

| | Orchid | Trudy |
|---|---|---|
| Builds for Ryzen 9000 (Zen 5) | generic x86-64 + PGO | also a **`-zen5`** build: `-march=znver5` + the same PGO profile |
| Dual-CCD Ryzen (7950X3D, 9950X3D, 9900X3D, 9950X...) | threads go wherever Windows puts them | hot threads kept **on one CCD**, the **3D V-Cache CCD** on X3D parts; no Game Bar or Process Lasso needed |
| RTX 40 / RTX 50 tensor instructions (HMMA) | software loop unless you set `EDEN_NX2_TENSOR=fast` | **tensor cores on by default** through `VK_KHR_cooperative_matrix` |
| GPU wait latency | driver sleep, wakes late | **50 µs spin** before sleeping on 12+ thread CPUs (Orchid's own option, now on by default there) |
| RTX 40 / 50 detection | Ampere and newer are one group | **Ada and Blackwell recognised** and logged, so later tuning can target them |
| MSVC builds with a preset | presets ignored | `zen4`/`zen5` → `/arch:AVX512`, `v3`/`zen2` → `/arch:AVX2` |
| Game compatibility | | same as the Orchid commit Trudy is based on (all of Orchid's fixes are kept) |
| Settings, keys, firmware, saves | `%APPDATA%\eden` | the same folder, so Trudy uses what Orchid already has set up |

If you are not on a Ryzen CPU or an RTX 40/50 card, Trudy behaves the same as Orchid: every
change detects the hardware and does nothing elsewhere.

## What Trudy changes

All changes against Orchid `52d4474` are in two commits; `git diff upstream/main` shows them.

**AMD Ryzen**

- `src/common/cache_affinity.cpp` (new): finds the CPUs that share each L3 cache (Windows:
  `GetLogicalProcessorInformationEx`; Linux: `/sys/devices/system/cpu/*/cache/index3`). With more
  than one L3 domain it keeps the hot threads on the largest one: guest CPU cores, core timing,
  GPU front and back threads, GPU fencing, the Vulkan worker and presentation. That avoids
  ~70-80 ns cross-CCD hops between threads that hand work to each other thousands of times a frame,
  and on X3D parts it keeps the macro memo and cache lookups inside the 96 MB V-Cache. With a
  single L3 (9800X3D, 7800X3D, 8-core parts, most Intel CPUs) nothing changes.
  `TRUDY_CCD_AFFINITY=0` turns it off.
- `CMakeLists.txt`: a `zen5` build preset (`-march=znver5 -mtune=znver5`, clang 19+ / GCC 14+);
  MSVC presets mapped to `/arch:AVX512` and `/arch:AVX2`.
- CI: `windows-x86_64-zen5` and `linux-x86_64-zen5` release files, built like the `-pgo` ones.
- The dynarmic JIT already emits AVX-512 code when the CPU has it. Zen 5 runs AVX-512 at full
  width, so Trudy keeps that on; `EDEN_NX2_AVX512=0` still turns it off if a game misbehaves.

**NVIDIA RTX 40 / RTX 50**

- `vulkan_device.cpp/.h`: `Arch_Ada` and `Arch_Blackwell`. Vulkan has no architecture id and
  Ada/Blackwell expose the same extensions as Ampere, so they are recognised by name (RTX 40xx,
  RTX xxxx Ada Generation, L4/L40 → Ada; RTX 50xx, RTX PRO, Blackwell → Blackwell). Turing and
  Ampere cards are never upgraded by mistake.
- `vk_pipeline_cache.cpp`: on Ada/Blackwell with `VK_KHR_cooperative_matrix`, the guest's HMMA
  matrix instructions run on the tensor cores (Orchid's experimental `fast` path) instead of a
  software loop of hundreds of instructions. `EDEN_NX2_TENSOR=soft` keeps the software path.
- `vk_master_semaphore.cpp`, `vk_scheduler.cpp`: `EDEN_NX2_GPU_WAIT_SPIN_US` defaults to 50 on
  CPUs with 12 or more threads; the waits are often for work that ends within microseconds.

**Name and icon**

- New orchid icon (`dist/trudy.svg`): white-and-pink orchids on deep teal, so it is easy to tell
  apart from Orchid's magenta-on-purple one. Rendered to the Windows `.ico`, the in-app icon, the
  SDL `.bmp`, the macOS `.icns` and the Linux `.svg`.
- `trudy.exe`, window titles, dialogs, translations, release files `trudy-*`, desktop id
  `dev.trudy.trudy`. The about box credits Orchid and Eden.
- The update checker reads this repository's releases. Versions restart at 0.1.

## Test results

Tested 2026-11-05 on an AMD Ryzen 7 9800X3D with an NVIDIA GeForce RTX 4090 (driver 621.01,
Vulkan 1.4.357), Windows 11.

| Test | Result |
|---|---|
| Driver exposes what the tensor path needs (`VK_KHR_cooperative_matrix`, `cooperativeMatrix`, `shaderFloat16`) | ✅ all present, so the tensor-core default switches on for this card |
| GPU name detection, run against the real `RefineNvidiaArchitecture` code | ✅ RTX 4090 / 4060 Laptop / RTX 6000 Ada / L40S / L4 → Ada; RTX 5090 / 5070 Ti / RTX PRO 6000 Blackwell → Blackwell; RTX 3090 / RTX A4000 stay Ampere; Quadro RTX 4000 stays Turing |
| L3 domain detection on the 9800X3D (Windows, compiled with MSVC 2026) | ✅ one L3 domain found, thread placement left to Windows (mask `0xffff` before and after), as intended on single-CCD parts |
| `TRUDY_CCD_AFFINITY=0` | ✅ skips the detection |
| CI builds (GitHub Actions) | ⏳ running: see [Actions](https://github.com/joshpack/Trudy/actions) |

**Not tested yet**

- **In-game FPS.** Measuring needs Switch 2 games and keys dumped from your own console. Orchid's
  numbers for its own test PC are below; Trudy numbers will be added as people report them.
  Please open an issue with your CPU, GPU, game and FPS for Orchid and Trudy side by side.
- **Dual-CCD Ryzen pinning on real hardware.** The detection code is the same as on the 9800X3D,
  but the pinning only does something on a two-CCD chip, which this test PC is not.
- **Tensor-core shaders in a game.** The `fast` path is Orchid's own experimental code; Trudy only
  turns it on by default for RTX 40/50. If a game shows broken effects, try `EDEN_NX2_TENSOR=soft`
  and please report it.
- **Linux.** The Linux code paths compile in CI but have not been run on hardware.

### Orchid's measurements (upstream, for reference)

The table below is Orchid's, copied unchanged. It was measured on a different PC (Ryzen 7 5800X3D,
Radeon RX 9070 XT) and shows game compatibility, which Trudy shares.


> [!NOTE]
> Measured by hand in November 2026 on one PC: AMD Ryzen 7 5800X3D, AMD Radeon RX 9070 XT,
> 32 GB RAM, CachyOS (Linux 7.x, Mesa RADV from git), 1x resolution scaling. Other hardware
> will differ.

| Game | FPS | Notes |
|---|---|---|
| Hyrule Warriors: Age of Imprisonment | 60 | after shader compilation |
| Donkey Kong Bananza | 30, 15-25 in heavy areas | 30 in lighter areas |
| Mario Kart World | 30-40 | lots of off-track asset glitches |
| Mario Tennis Fever | 60 | runs flawlessly |
| Yoshi and the Mysterious Book | | gameplay renders a black screen |
| Super Mario Bros. Wonder | 60 | runs flawlessly |
| Star Fox | 30 | crashes very quickly |
| Splatoon Raiders | 30 | in gameplay |
| Pokémon Pokopia | 60 | in gameplay |
| Pokémon Legends: Z-A | 30 | in gameplay, the Switch 1 frame cap |
| Orbitals | | does not launch |
| Nintendo Switch Sports Resort | 60 | broken lighting, broken controller selection |
| Nintendo Switch 2 Welcome Tour | 30 | your own character is fully black |
| Metroid Prime 4: Beyond | 60 | until it crashes early on |
| Kirby Air Riders | | crashes while loading a race |
| Fire Emblem: Fortune's Weave | 30 | in the first fight, large graphics glitches top left |
| Fast Fusion | | stuck loading into a race, then the emulator crashes |
| Drag x Drive | | no controller support, not playable |


## Settings

Trudy reads the same environment variables and `nx2_switches.txt` file as Orchid. The ones Trudy
adds or changes:

| Variable | Trudy default | Meaning |
|---|---|---|
| `TRUDY_CCD_AFFINITY` | on | `0` lets Windows/Linux place the hot threads anywhere |
| `EDEN_NX2_TENSOR` | `fast` on RTX 40/50 | `soft` runs tensor instructions in software, as Orchid does |
| `EDEN_NX2_GPU_WAIT_SPIN_US` | `50` on 12+ thread CPUs | `0` turns the spin off |
| `EDEN_NX2_AVX512` | on | `0` makes the JIT use AVX2 code only |

Recommended in-app settings on an RTX 4090: resolution scaling 2x-3x. The GPU is mostly idle
in these games, so higher internal resolution costs almost no frame rate. Keep Orchid's per-game
defaults for GPU accuracy.

## Download

Releases are on the [releases page](https://github.com/joshpack/Trudy/releases). Each one has:

| File | For |
|---|---|
| `trudy-<version>-windows-x86_64-zen5.zip` | **Windows on Ryzen 9000 / 9000X3D (Zen 5)** |
| `trudy-<version>-windows-x86_64-pgo.zip` | Windows, any x86-64 CPU, profile-guided (preferred elsewhere) |
| `trudy-<version>-windows-x86_64.zip` | Windows, MSVC build |
| `trudy-<version>-linux-x86_64-zen5.AppImage` | Linux on Ryzen 9000 / 9000X3D |
| `trudy-<version>-linux-x86_64-pgo.AppImage` | Linux, profile-guided |
| `trudy-<version>-linux-x86_64.AppImage` | Linux |
| `trudy-<version>-macos-arm64.zip` | macOS 15+ on Apple Silicon, experimental |

The `-zen5` builds will not start on older CPUs (they use AVX-512 and other Zen 5 instructions);
use `-pgo` there. Windows: extract the zip and run `trudy.exe`. Linux: `chmod +x` the AppImage
and run it; it needs glibc 2.39 or newer.

## Build it yourself

Linux. Install the dependencies once:

```sh
# Arch / CachyOS
sudo pacman -Syu --needed base-devel boost catch2 cmake enet ffmpeg fmt git glslang libzip lz4 \
  ninja nlohmann-json openssl opus qt6-base qt6-multimedia qt6-charts qt6-wayland sdl3 zlib zstd \
  zip unzip vulkan-headers vulkan-utility-libraries libusb spirv-tools spirv-headers
```

Ubuntu 26.04, Debian 13 and other distributions: see [docs/Deps.md](docs/Deps.md).

Clone, configure and build. `-DYUZU_BUILD_PRESET=zen5` targets Ryzen 9000; use `native` to target
exactly the CPU you build on, or leave it out for a portable build:

```sh
git clone https://github.com/joshpack/Trudy.git
cd Trudy
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DYUZU_TESTS=OFF \
  -DYUZU_BUILD_PRESET=zen5 \
  -DVulkanHeaders_FORCE_BUNDLED=ON -DVulkanUtilityLibraries_FORCE_BUNDLED=ON
cmake --build build
./build/bin/trudy
```

Windows (MSVC or MSYS2 clang) and macOS builds are described in the [build guide](docs/Build.md);
the presets work there too. A self-built binary has no update checker; `git pull` is the update.

To bring in new Orchid fixes:

```sh
git remote add upstream https://github.com/MythosOfs/Orchid.git
git pull upstream main
```

## Documentation

- [User handbook](docs/user/README.md)
- [Build guide](docs/Build.md) and [dependencies](docs/Deps.md)
- [Fire Emblem: Fortune's Weave bring-up notes](docs/FE_FORTUNES_WEAVE.md)
- [Switch 2 loading notes](NX2_NOTES.md)
- [Development tools](tools)
- [Fork scan: taking fixes from Eglantine forks](docs/ForkScan.md)

## Credits

Trudy is built on [Orchid](https://github.com/MythosOfs/Orchid) by MythosOfs and everyone credited below; all of the emulation work is theirs. Orchid's own credits follow.

Orchid takes fixes from Eglantine and its forks; after every release
[tools/fork-scan.sh](docs/ForkScan.md) lists what they fixed since. Each ported commit names its
source and author in its message.

- [zortos293/Eglantine](https://github.com/zortos293/Eglantine) (Zortos), the project Orchid
  started from: paged scalar guest memory access, SPSCQueue value lifetime, timed Event waits on
  Windows, GPU teardown order, buffer cache and query fixes, native Ampere nanosecond timestamps,
  the separate pipeline cache file names.
  Radu Ursache: no mesh pipelines without `VK_EXT_mesh_shader`.
  Kaj (co-authored with Claude Opus 5.5): skip guest uploads for new render targets fully
  overwritten by a clear, adapted with framebuffer render-area, transformed-scissor,
  linear-attachment and zero-layer guards from
  [27226d2568c99c70cf78aa37adaf98b60457b2e5](https://github.com/zortos293/Eglantine/commit/27226d2568c99c70cf78aa37adaf98b60457b2e5).
  Paulo Tumabing: 32-bit global `ATOM.CAS`, adapted to Orchid's storage lowering and atomic routing.
- [Djihads80/Eglantine](https://github.com/Djihads80/Eglantine) and its contributors: merged
  regularly. Idle staging buffers freed (Takoroka), the RangeSet empty-query guard (néo Luigi),
  test fixes (kraibse).
- [ZnOw01/Primrose](https://github.com/ZnOw01/Primrose) (ZnOw01): shader cache invalidation
  lifetime fix, adapted to renderer-thread retirement.
- [exploitizm/Eglantine](https://github.com/exploitizm/Eglantine) (exploitizm): signed IADD32
  negative-overflow boundary fix for GLSL and SPIR-V, adapted from
  [c2f9811073bfe617524d4413742a4d4406fd287a](https://github.com/exploitizm/Eglantine/commit/c2f9811073bfe617524d4413742a4d4406fd287a).
- [Selevet Eglantine](https://github.com/sylvieistaken/selevet-eglantine) (sylvieistaken): the
  windowed GPU mapping table diff, hot loop shortcuts, F2I, local spill and texture query lowering,
  device memory rebuild on layout changes, decoder end of stream, Windows commit retries, and the
  GPU wait knobs (`EDEN_NX2_GPU_WAIT_SPIN_US`, `EDEN_NX2_BACK_SPIN`, `EDEN_NX2_ACQUIRE_QUEUED`,
  `EDEN_NX2_SWAP_STABLE`, `EDEN_NX2_AVX512`).
- [PokeDinner](https://git.eden-emu.dev/Franseven/PokeDinner-a-Eglantine-Fork) (Franseven,
  [mirror](https://github.com/Apollo422/PokeDinner)), partly from Eden-NS2: any cache hint on
  native LDG, SUST with a partial channel mask, `EDEN_AUTO_BUTTONS`.
- Forks of those: Dee1, Greencus and exploitizm (audio manager lifetime, macro binds past the
  start table), LucyferDev (A64 saturating shift and multiply-accumulate instructions), FZout (page
  table block ids, game list titles), sagaopc (NUP directory cycles), Quixomatic (eden-cli, Switch 2
  module names), YLTai (SDL motion after reconnect), Aidan Kinzett and oledz-i (NUP detection,
  geometry stream 0), NicholasBly and ominixisboss (dynarmic), kipavy (storage CAS loop),
  adamhahnHU and arisqz (sparse layered images), Pavel Barabanov and andrewleeog (F2I bits, host1x
  syncpoint actions), MFS-code (Switch 2 GOB order), CarlosKeese and Dennys Kaluzny Pereira
  (lowering), failaip12 (memory manager bounds).
- After 0.6.3: godpow and its contributors nowhere02, xDarkWindx, Duck912, ZnOw01 and CloudP4bbs
  (buffer, texture and staging cache guards, descriptor growth, GPU split wake-up recovery, NPad
  update filter), failaip12 via cira0100 (data races, UBSan fixes, audio and SDL locking),
  monteger8 for adamd (Donkey Kong Bananza buffer cache skips, indirect draw reset by hash), Aidan
  Kinzett (NV12 video decoder), Jonas via jojo-chaechae (x64 call reach, Switch 2 channel setup),
  SavageCore (mod root guards), cblock85, christianball123, Khobzpro, larryliu985, Doughston
  (n-popescu) and Xnos666.
- After 0.7.0: Zortos (native BRA.U, register-handle image access, uniform shifts, global memory
  helpers, the H.264 decoder reset, Aftermath dumps, Switch 2 mixer stream audio), zirulo via Zortos
  (the Ocarina of Time boot fix), Paulo Rossatto (loose base dumps, GPU-only regions, 2D blit
  formats), Raikaru (ssl:s contexts and socket timeouts), Mcm190 (H.264 packet framing), Yuri Olive
  (decoder end of stream), rdenno (fence download ranges), kraibse (tessellation pipelines), a55uka
  (contentless NUP sections), Andreas Heider (ANV barriers), Aidan Kinzett (button glyphs), Jonas
  (H0_NH1 halves), exploitizm (IMAD, I2F and FSET lowering, memory manager reference counts),
  christianball123 (device index fallback), kdegonia-star (global atomic add) and Takoroka (ISETP.EX
  and UIMAD.WIDE lowering).
- rdenno: the Mario Tennis Fever fixes in 0.5.1.
- [Drizz1le/Lichii](https://github.com/Drizz1le/Lichii) (Inc0rporate, with Claude Opus 5.5):
  count nested shader returns before reordering demotes for AMD drivers.
- [DrSergioRFilho/Lichii](https://github.com/DrSergioRFilho/Lichii) (Dr. Sergio, with Claude
  Fable 5.1): apply LayeredFS mods to NUP titles without reapplying their merged update, from
  [fe29f5c45ef8f8ab826f148827a1b3962c32d7d7](https://github.com/DrSergioRFilho/Lichii/commit/fe29f5c45ef8f8ab826f148827a1b3962c32d7d7).
- [chuppatricky/Lichii](https://github.com/chuppatricky/Lichii) (chuppatricky, with Claude Opus 5.5):
  shadow Ampere RED and SURED data registers, adapted from
  [fc5d6dc31b49ae614d06c05302f4ee2abdf8ce4c](https://github.com/chuppatricky/Lichii/commit/fc5d6dc31b49ae614d06c05302f4ee2abdf8ce4c).
- [SoyMoikas/Eglantine](https://github.com/SoyMoikas/Eglantine) (SoyMoikas): uniform-base
  native Ampere local stores and loads, adapted to Orchid's existing vector lowering and register
  shadowing from
  [3c30f286d9631581ea3611c0f30eae46e7f60694](https://github.com/SoyMoikas/Eglantine/commit/3c30f286d9631581ea3611c0f30eae46e7f60694)
  and
  [8c06623a4651e5f31d7297932f6b9887dfea9006](https://github.com/SoyMoikas/Eglantine/commit/8c06623a4651e5f31d7297932f6b9887dfea9006).
- [Hydro74000/Edlws](https://github.com/Hydro74000/Edlws) (Hydro74000): make `UniqueFunction`
  own copies of lvalue callables while moving rvalues, from
  [dd8039f6afc92803938d641a484312931acae9ca](https://github.com/Hydro74000/Edlws/commit/dd8039f6afc92803938d641a484312931acae9ca).
- [DrSergioRFilho/Lichii](https://github.com/DrSergioRFilho/Lichii) (Dr. Sergio,
  with Claude Fable 5.1): check nested config array bounds against the innermost array, from
  [08d3388f0f5496be67dd5f83dfdcddd0fcd59358](https://github.com/DrSergioRFilho/Lichii/commit/08d3388f0f5496be67dd5f83dfdcddd0fcd59358).
- [Shenopi/Lichii](https://github.com/Shenopi/Lichii) (999sian): preserve active custom settings
  while reloading global configuration, adapted from the config-only changes in
  [88ffd1f312f9a38fd45575dc8e1c7d66ef9b4f6e](https://github.com/Shenopi/Lichii/commit/88ffd1f312f9a38fd45575dc8e1c7d66ef9b4f6e).
- [trianglebraindev/Lichii](https://github.com/trianglebraindev/Lichii) (trianglebraindev,
  with Claude Opus 5.5): two-source `F2IP.U8.F32` byte packing, adapted to Orchid's register
  shadowing from
  [ac82c78b5ca4cfbcb11e3043f05a3354f444f027](https://github.com/trianglebraindev/Lichii/commit/ac82c78b5ca4cfbcb11e3043f05a3354f444f027).

- [leadaenima/Lichii](https://github.com/leadaenima/Lichii) (Cursor Agent, co-authored with
  leadaenima): AMDVLK dynamic subgroup shuffle workaround, adapted from
  [957913cf14ca1e3776a4984151b63434af543217](https://github.com/leadaenima/Lichii/commit/957913cf14ca1e3776a4984151b63434af543217)
  and [201450e90a87b494a8d1dc34d32558033a715438](https://github.com/leadaenima/Lichii/commit/201450e90a87b494a8d1dc34d32558033a715438).
  Graphics use the 32-lane broadcast fallback only when subgroup size 32 can be enforced for
  that stage. Compute and unsupported graphics stages retain Shuffle; Qualcomm keeps its
  existing full-subgroup fallback. Pipeline hash diagnostics are omitted.

## License

GPLv3 or any later version, see [LICENSE.txt](LICENSE.txt). Forked from [Orchid](https://github.com/MythosOfs/Orchid), which started from
[Eglantine](https://github.com/zortos293/Eglantine), which builds on
[Eden](https://git.eden-emu.dev/eden-emu/eden), yuzu and Sudachi.
