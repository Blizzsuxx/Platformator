# Runtime-only and installed-consumer evidence

## Linux editor-off/session-off package smoke (2026-11-07)

The integrated Linux Debug suite ran the existing `platformator_package_smoke` test after the editor install work. That test configures a Release SDK with `PLATFORMATOR_BUILD_EDITOR=OFF`, the default `PLATFORMATOR_ENABLE_EDITOR_SESSION=OFF`, examples disabled for the core package, then installs and relocates public consumers and the Mario sample to a second prefix. It builds the installed consumer, a source-tree consumer, and the installed Mario target, and runs both relocated executables from an unrelated working directory.

```text
ctest --test-dir bin/editor-debug -R '^platformator_package_smoke$' --output-on-failure --timeout 240
ctest --test-dir bin/editor-debug --output-on-failure --timeout 240 -j 4
```

Outcome: the isolated package smoke passed 1/1 in 116.69 seconds from a partially built fixture; the final integrated run passed all 69 registered tests (68 passed, 1 expected benchmark-contract skip), with package smoke completing incrementally in 6.69 seconds. This confirms the public SDK consumer lane builds without enabling the editor or session protocol. It does not yet constitute the full T144 audit of dependency metadata and normal runtime behavior under both editor/session-off configurations, nor the absent native Windows/macOS package lanes.

`ldd` on the relocated installed `consumer` and `mario_example` showed their SDL3 and oneTBB dependencies resolving from the same relocated prefix. A focused dependency scan returned no `imgui`, `platformator_editor`, `dbus`, or unresolved-library entries. The package smoke runs the installed and relocated Mario executable for five smoke steps under SDL's dummy/software drivers; this does not replace the full runtime step/render checks required by T143/T144.

## Current editor-off/session-off lane (2026-11-07)

Made the package smoke configure `PLATFORMATOR_BUILD_EDITOR=OFF` and `PLATFORMATOR_ENABLE_EDITOR_SESSION=OFF` explicitly, and connected it to `cmake/PlatformatorEditorValidation.cmake` for future integrated Linux validation runs. The updated package smoke passed 1/1 in 5.97 seconds using its existing build cache. It rebuilt and ran installed, relocated, and source-tree public consumers from an unrelated working directory and built/ran the relocated Mario smoke sample. The consumer exercises headless Runtime construction, a simulation step, Scene object/component creation and teardown.

The isolated core cache records both options as `OFF`. `ldd` on `build/package-smoke/final-prefix/bin/consumer` resolves SDL3 and oneTBB from the relocated prefix; `readelf -d` reports no ImGui, editor, or accessibility-provider dependency. This closes the Linux runtime-only consumer lane. It does not claim Windows/macOS package validation.

```text
ctest --test-dir build --output-on-failure -R '^platformator_package_smoke$'
ldd build/package-smoke/final-prefix/bin/consumer
readelf -d build/package-smoke/final-prefix/bin/consumer
```
