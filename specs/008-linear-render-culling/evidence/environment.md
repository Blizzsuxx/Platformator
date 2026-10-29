# T044 Capture Environment

Captured for the regenerated run `20261029-t044-run-01` on 2026-10-29 (Europe/Belgrade). Machine/build metadata is also retained with the per-run manifests and `provenance/environment.txt`.

## CPU, workers, and affinity

- Host: x86_64, AMD Ryzen 5 7535HS with Radeon Graphics.
- Topology: one socket, six physical cores, two threads per core, 12 online logical CPUs (0–11).
- Available comparison worker counts: 1 and 6; six is the available physical-core worker count.
- Timing driver and child processes were pinned to CPUs 0–11. The driver manifests record the inherited affinity; six workers were not presented as a separate parallel rendering algorithm.

## Renderer, output, and VSync

The actual focused validation/timing probes queried SDL and recorded renderer `software`, 320×240 output, scale `[1.0, 1.0]`, viewport `[0,0,320,240]`, no active target, no logical presentation, and VSync 0. Headless probes created no renderer. Environment variables were set to SDL dummy video/audio and software rendering; the queried metadata, not those variables, is the renderer evidence. Stock-runner manifests omit renderer identity and remain explicitly unverified for that field.

## Build tools and dependencies

- CMake 4.4.4; Ninja 1.13.2; GCC/G++ 16.2.1; Python 3.14.7.
- Release builds use C++20, `-O3 -DNDEBUG`, debug tools off, sanitizer none.
- Separate work-probe-on validation binaries and work-probe-off timing binaries were built for baseline and candidate.
- Bundled SDL 3.4.18, SDL_image 3.4.8, SDL_ttf 3.2.2, SDL_mixer 3.2.4, Eigen 5.0.1, oneTBB 2022.3.0, and nlohmann JSON.

## Persistent artifacts and earlier inventory

The T044 capture roots are under the ignored `benchmark/artifacts/linear-render-culling/20261029-t044-run-01/` directory. The earlier 2026-10-28 `/tmp` inventory and final 2026-10-29 `/tmp` roots cited in the pre-T044 summaries were absent when this regeneration began. Their exact old summaries are preserved as superseded text in the bundle; the regenerated run is the current evidence. The original benchmark resource and report inputs needed for verification are not stored in `/tmp`.
