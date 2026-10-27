# Mario baseline and compatibility

The baseline and candidate production Mario executables passed five-step smoke tests with dummy SDL drivers. A separate host linked against each tree's Mario behavior objects loaded `examples/mario/level1.scene`, ran a simulation step, saved the scene to a temporary file and verified the `MarioCameraRig` behavior retained `cameraLead: 90`. Both passed. The host and logs are retained under `work/`. No graphical gameplay comparison is inferred from a dummy-driver smoke test.

The same round-trip host was rebuilt against the final candidate after removing unused internal path-resolution parameters; both baseline and candidate again preserved `cameraLead: 90`.
