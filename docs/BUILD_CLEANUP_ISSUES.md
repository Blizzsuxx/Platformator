# Build configuration findings

Recorded before the preset simplification.

- Seventeen presets expose overlapping Release/Debug builds and historical cleanup
  campaigns. `debug` disables the editor, contrary to its expected purpose.
- Build features, test counters, selected products and compiler modes are exposed
  as independent switches, allowing inconsistent combinations and stale caches.
- Editor Build & Run repeats build policy through long lists of low-level `-D`
  overrides. Its repository fallback check still uses the former source layout.
- Editor session support is presented as optional even though normal editor Play,
  Pause and Step need it in both Release and Debug.
- Engine/editor tests and work counters require coordinated instrumentation;
  benchmark timing builds need that instrumentation disabled.
- Capture tools, validation scripts and defaults refer to retired preset names.
- SDK/source consumers must continue building only the engine. Installation and
  sanitizer integration remain necessary advanced workflows.

The cleanup keeps four daily presets: default, debug, test and benchmark.
Compiler mode comes from CMAKE_BUILD_TYPE. BUILD_TESTING controls tests and their
observation hooks. Benchmark products are selected independently. Editor-session
support is part of the normal runtime, including optimized builds. Source/SDK
consumers retain their existing exported target and public include names.

## Changes made

- Replaced seventeen overlapping presets with four visible configure/build
  profiles: `default` (Release editor), `debug` (Debug editor), `test`, and
  `benchmark`. Test presets run the appropriate correctness suites.
- Reduced public feature selection to editor, tests, and benchmark products.
  Compiler mode determines debug tools; tests determine observation hooks.
  Old feature cache entries are removed during reconfiguration.
- Kept the generic runner available as an explicit target without a runner
  switch. Games build through their standalone projects; the ordinary editor
  configuration no longer visits game assets/sources.
- Kept editor-session support in ordinary runtime builds, including Release.
  Fixed repository detection after the engine directory move and simplified
  the editor's game configure commands and bundled defaults.
- Updated standalone CMake entry points, SDK validation scripts, current capture
  defaults and user-facing build documentation. Installation and sanitizers
  remain available as advanced settings; historical reports remain historical.
- Restored the public BUILD_TESTING cache entry after Eigen configures its own
  dependent option. Updated capture provenance checks to read the new selectors
  and locate manifests under the component build directory.

Existing custom editor overrides are preserved. Settings referencing a retired
preset or executable path need to be updated explicitly. Old generated build
directories are left in place; reconfigure through the new profile rather than
moving cached build trees.
