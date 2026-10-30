# Incremental Render Culling Reference Provenance

## Frozen feature-008 reference

The `current-linear` reference is extracted from the verified T044 feature-008 archive at `benchmark/artifacts/linear-render-culling/009-20261029-run-01/reference/source/Platformator/`. The archive SHA-256 is `d8c100e7d7595a0cf15e88d23c29f36e1fcc14025c6e15714b4c707c653e58a7`; its original input manifest SHA-256 is `9d1d58700549c59511313c5ee016d3be7557d7ac72464afd36b8874590090dc7`. The original aggregate fingerprint across 132 recorded inputs is `4a5ce52e10fc81f903c3687879ee9d883836ec13668e18185f967fc6fa20dfba`.

The reference tree's `src/`, public `include/platformator/`, and `assets/` remain byte-identical to the archived source. The audited common support patch changes only these six harness/fingerprint files:

| File | Original SHA-256 | Patched SHA-256 |
|---|---|---|
| `CMakeLists.txt` | `93da6cfb08e748ad3610132e7b1e09d9b440c0e9165ec01226ffaf1431f71c9e` | `4c197cbf5e83e6b30cadfcc9192cbb3e3d0ee78968c8258428833ae8c8f4f238` |
| `benchmark/capture_linear_render.py` | `0cf4ca8b9b2666142d683cf1839baa89d049d4e6b52c9c68a40ed61ec0867cd4` | `60d7d077ff6983e885ac22dffdb69c290c1b52d768ee9dbb1db865912f3c6691` |
| `benchmark/linear_render_probe.cpp` | `cfe2d6bdc72997a8d0f9b1e9716b9cff274515bb93690f87ce588a09595026b8` | `cb9dacfd65803d3118a38e1acb2c1af3977b91a0f8ceb699610d6a876c40cd26` |
| `benchmark/tests/test_linear_render.py` | `6173fc96ba7c82f13cf56d7e6b24e836057fdaf4761c8ff853db31fe78165462` | `5785b63b2338b0693cd11ff18ba20aeafac9b85f5a61916adf5b9f4535a972d6` |
| `tests/render_culling_fixture.h` | `caff033f8a7577eb7d3f9eafe5a897bb09a70db78b385a7d010d4c174d84e65d` | `307f7abb3d4d7dfccf4c066818fa19b2f5bb604080c51b51958fef51552e4161` |
| `tests/runtime_work_probe.h` | `e6b940de5f8ee5abb10020c66c47934bf1dbc0901c56dff2219fc76377ae3644` | `59344cd320734131a96fa2d605f53feb21cd25b132ab9947e910318927fa47b6` |

The complete per-file byte counts and checksums are in `reference/provenance/support-patch-manifest.json`. The unified patch SHA-256 is `960c949c271fee546defebc14de4ee7dc988df959811dfc9433c6f5130251f95`; `engine_runtime_modified` is false. The patch adds the shared benchmark probe/capture support and final fingerprint inputs, while the candidate-only culling tests are registered only when the candidate source module exists.

The verified build identity matrix is retained in `benchmark/artifacts/linear-render-culling/009-20261029-run-01/provenance/final-build-identities.json`. Reference validation and timing share source fingerprint `0486bb813e965488c62a0aa4c05ef5bba62ef0e17049bac5f078caf89d6d6cd0`; candidate validation and timing share `d3418214c4f48416d943e95e44e8d8d3a7b296ae3ff23b3dd3d4c7d69948b93f`. All four use fixture `307f7abb3d4d7dfccf4c066818fa19b2f5bb604080c51b51958fef51552e4161` and harness `3c9ffb4501f38fc4661a4e46294582548e7485b5e69a84702e613226cc9fcda3`. Builds are Release, debug tools off, sanitizer none; validation has work probes enabled, timing has them disabled.

The T044 source archive and historical timings remain unchanged. The current working tree contains feature-009 implementation changes and is not used as the reference source.
