> [!IMPORTANT]
> ## Report crashes from inside Trudy
> If a game crashes, start Trudy again and pick **Help > Report a Crash on GitHub**. It opens a bug report on the Trudy repository with your build, system, GPU and driver and the end of the crash log already filled in, and opens the log folder so you can attach the full log of the crashed run.

## Trudy 0.1

The first Trudy release: Orchid 0.7.5 with all of its game fixes (and Orchid's newest shader fixes), plus performance work found by profiling a real game.

**Ocarina of Time on a Ryzen 7 9800X3D + RTX 4090: 56 FPS where the same scene and settings ran at 35 FPS (+61%), with the graphics card 38% busy instead of 55%** (30 FPS patch off to show the full rate; details in the README).

### Faster on every PC

- GPU results are copied back to the game only for the pages its CPU actually reads. Before, one read page sent the GPU's whole written range back after every write: 8.6 GB/s over PCIe in Ocarina of Time, now 0.7 GB/s. `TRUDY_PAGE_DOWNLOADS=0` restores the old behaviour.
- Ocarina of Time's recommended settings now turn reactive flushing off (faster now: 17.9 instead of 22.9 ms a frame).
- Switch 2 games without recommended settings get what all of Orchid's presets share (GPU accuracy Low, Balanced fences, asynchronous shaders, reactive flushing off), unless you chose those settings yourself. `TRUDY_SWITCH2_DEFAULTS=0` turns this off.
- GPU waits spin for 50 µs before sleeping on CPUs with 12 or more threads.

### AMD Ryzen

- New `-zen5` downloads for Ryzen 9000 / 9000X3D (`-march=znver5`, with the PGO profile). They do not start on older CPUs; use `-pgo` there.
- On dual-CCD Ryzen (7950X3D, 9950X3D, 9900X3D, 9950X...) the emulator's hot threads stay on one CCD, the 3D V-Cache CCD on X3D parts. `TRUDY_CCD_AFFINITY=0` turns it off.

### NVIDIA RTX 40 / RTX 50

- RTX 40 (Ada) and RTX 50 (Blackwell) cards are recognised. The tensor-core path for the games' matrix instructions can now switch on there (`EDEN_NX2_TENSOR=fast`, experimental); it stays off by default.

### Also

- Profiling tools: `TRUDY_GPU_TIMING=1` and `TRUDY_GPU_TRACE=1` log the host GPU's time per shader, render pass and command.
- New name and orchid icon; `trudy.exe`. Settings, keys, firmware and saves are shared with Orchid (`%APPDATA%\eden`).

### Known issues

- Only Ocarina of Time was available for testing. Please report FPS for Orchid and Trudy side by side in other games.
- Everything listed under Orchid 0.7.5's known issues still applies.
