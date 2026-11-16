# SDL memory leak investigation — 2026-11-16

The previously reported 200-byte leak is triggered by **SDL_mixer**, through
FluidSynth's system dependencies **libjack** and **libgomp**. Both dependencies
reproduce their share with only `dlopen`/`dlclose`, without calling any SDL,
FluidSynth, JACK or OpenMP APIs. SDL_image is absent from this reproducer.
This narrows the earlier [FluidSynth attribution](../SDL_UPDATE_VALIDATION.md#leaks-persist-without-engine-code).

## Fresh leak scans

| Probe | Cycles | Leaked bytes | Allocations | Exit |
| --- | ---: | ---: | ---: | ---: |
| SDL + mixer init/quit | 0 | 0 | 0 | 0 |
| SDL + mixer init/quit | 1 | 200 | 4 | 1 |
| SDL + mixer init/quit | 10 | 2,000 | 40 | 1 |
| Load/unload system FluidSynth | 1 | 200 | 4 | 1 |
| Load/unload libgomp | 1 | 104 | 2 | 1 |
| Load/unload libgomp | 10 | 1,040 | 20 | 1 |
| Load/unload libjack | 1 | 96 | 2 | 1 |
| Load/unload libjack | 10 | 960 | 20 | 1 |
| SDL_image load/destroy PNG | 10 | 0 | 0 | 0 |

Exit 1 is LeakSanitizer's failure status. These are real leak reports, with no
suppressions or disabled decoders. All completed scans ran outside the sandbox
with `ASAN_OPTIONS=detect_leaks=1` and `UBSAN_OPTIONS=halt_on_error=1`.
An initial sandbox attempt failed because LeakSanitizer cannot inspect threads
under its ptrace restrictions; it supplies no leak result.

Runtime versions: SDL 3.4.18, SDL_mixer 3.2.4, SDL_image 3.4.8. System packages:
FluidSynth 2.6.1-1, jack2 1.9.22-2, gcc-libs
16.2.1+r23+gd564253eb6c8-1. The SDL probes reuse the existing instrumented
`bin/cleanup-asan` libraries; the engine was neither rebuilt nor linked into
these probes. [summary.json](summary.json) retains identities and hashes of the
actual loaded libraries, probe sources and PNG.

## Allocation ownership and trigger

The call chain is `MIX_Init → InitDecoders → FLUIDSYNTH_init →
LoadModule_fluidsynth → SDL_LoadObject → dlopen`. Mixer initializes this decoder
even if the application never plays a MIDI file. `MIX_Quit` unloads the module.
FluidSynth's ELF dependencies include JACK and libgomp; their load constructors
allocate the memory that survives unloading.

The standalone loader prints module bases before unloading, allowing otherwise
`<unknown module>` allocation addresses to be assigned to the loaded libraries:

| Allocation size | Module and return-address offset |
| --- | --- |
| 96 bytes (`calloc`) | libgomp.so.1 + 0xde4d |
| 8 bytes (`malloc`) | libgomp.so.1 + 0xde15 |
| 48 bytes (`operator new`) | libjack.so.0 + 0x83cc |
| 48 bytes (`operator new`) | libjack.so.0 + 0x83eb |

Independent loading of each library reproduces exactly these amounts, and ten
cycles multiply each by ten. Debug symbols are unavailable for the system
libraries, so exact source-line attribution is not claimed. Nearest exported
symbol names from `addr2line` on these stripped libraries are misleading and
are not used as function attribution.

There is related upstream evidence: a glibc maintainer attributes a
[libgomp unload leak](https://sourceware.org/pipermail/glibc-bugs/2024-December/058551.html)
to CPU-affinity storage and suggests cleanup in GCC. This supports the class of
failure; it does not identify both allocations in our GCC build. JACK's
[open pull-request listing](https://github.com/jackaudio/jack2/pulls) includes
“Release client global mutexes at shutdown” (#1025). Its patch was not fetched
or tested here, so it is a follow-up lead, not a verified fix.

`SDLWindow::init()` calls `MIX_Init()` and `SDLWindow::close()` calls
`MIX_Quit()` after destroying the mixer. `GameManager` owns one SDLWindow for
its lifetime. Thus repeated desktop runtime creation/destruction can exercise
the leak; ordinary scene switches reuse that window. Headless initialization
returns before mixer startup. These probes establish growth per module reload,
not per frame or image load. The PNG control covers this particular loading
path, not every SDL_image codec or engine resource lifetime.

## Remediation direction

The direct repair belongs in JACK/libgomp unload cleanup. Another approach to
evaluate is a controlled FluidSynth build without its unused hardware audio
drivers and OpenMP dependency, preserving MIDI synthesis through SDL_mixer.
Both approaches need fresh leak scans and audio/MIDI correctness checks before
being called fixed. Changing the selected playback device will not prevent
ELF dependency constructors from running.

Keeping libraries permanently loaded can prevent repeated constructor runs but
does not prove cleanup; it is not treated as a leak fix. Disabling FluidSynth
changes decoder support. Neither workaround, a system package change, nor an
engine/vendor patch was applied during this investigation.

## Reproduce

Run from the repository root. Builds remain under `build/`. The following uses
the existing ASan SDL libraries and builds only three tiny diagnostics:

```sh
mkdir -p build/sdl-leak-investigation
c++ -g -O0 -fsanitize=address -fno-omit-frame-pointer \
  docs/sdl-leak-investigation/dlopen-probe.cpp -ldl \
  -o build/sdl-leak-investigation/dlopen-probe
c++ -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer \
  docs/sdl-update-evidence/mixer-probe.cpp \
  -Ivendored/SDL3-3.4.18/include -Ivendored/SDL3_mixer-3.2.4/include \
  -Lbin/cleanup-asan -Wl,-rpath,"$PWD/bin/cleanup-asan" -lSDL3_mixer -lSDL3 \
  -o build/sdl-leak-investigation/mixer-probe
c++ -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer \
  docs/sdl-leak-investigation/image-probe.cpp \
  -Ivendored/SDL3-3.4.18/include -Ivendored/SDL3_image-3.4.8/include \
  -Lbin/cleanup-asan -Wl,-rpath,"$PWD/bin/cleanup-asan" -lSDL3_image -lSDL3 \
  -o build/sdl-leak-investigation/image-probe
export ASAN_OPTIONS=detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1
timeout 20s build/sdl-leak-investigation/mixer-probe 1
timeout 20s build/sdl-leak-investigation/dlopen-probe /usr/lib/libfluidsynth.so.3 1
timeout 20s build/sdl-leak-investigation/dlopen-probe /usr/lib/libgomp.so.1 1
timeout 20s build/sdl-leak-investigation/dlopen-probe /usr/lib/libjack.so.0 1
timeout 20s build/sdl-leak-investigation/image-probe assets/textures/missing.png
```

Use mixer counts 0 and 10 and direct-library counts 10 for the other table rows.
The actual capture used a 20-second subprocess timeout and 200,000-byte output
check per process. All nine completed quickly; their combined raw logs are
under 200,000 bytes. [Compressed raw logs](probe-logs.txt.gz) retain the stacks
and module maps. No full engine test campaign or benchmark was run.
