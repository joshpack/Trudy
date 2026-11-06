> [!IMPORTANT]
> ## Report crashes from inside Trudy
> If a game crashes, start Trudy again and pick **Help > Report a Crash on GitHub**. It opens a bug report on the Trudy repository with your build, system, GPU and driver and the end of the crash log already filled in, and opens the log folder so you can attach the full log of the crashed run.

## Trudy 0.1

The first Trudy release: Orchid 0.7.5 (commit 52d4474) tuned for AMD Ryzen CPUs and NVIDIA RTX 40 / RTX 50 graphics cards. All of Orchid's game fixes are included.

### AMD Ryzen

- New `-zen5` downloads for Ryzen 9000 / 9000X3D (`-march=znver5`, with Orchid's PGO profile). They do not start on older CPUs; use `-pgo` there.
- On dual-CCD Ryzen (7950X3D, 9950X3D, 9900X3D, 9950X...) the emulator's hot threads stay on one CCD, the 3D V-Cache CCD on X3D parts. Single-CCD CPUs such as the 9800X3D are unchanged. `TRUDY_CCD_AFFINITY=0` turns it off.

### NVIDIA RTX 40 / RTX 50

- RTX 40 (Ada) and RTX 50 (Blackwell) cards are recognised.
- On them, the games' tensor (HMMA) instructions run on the GPU's tensor cores by default instead of a software loop. `EDEN_NX2_TENSOR=soft` restores the software path if a game shows broken effects.

### All systems

- GPU waits spin for 50 µs before sleeping on CPUs with 12 or more threads (`EDEN_NX2_GPU_WAIT_SPIN_US=0` turns it off).
- New name and orchid icon; `trudy.exe`. Settings, keys, firmware and saves are shared with Orchid (`%APPDATA%\eden`).

### Known issues

- In-game frame rates have not been measured for Trudy yet; Orchid's compatibility list applies. Please report your FPS for Orchid and Trudy side by side.
- Everything listed under Orchid 0.7.5's known issues still applies.
