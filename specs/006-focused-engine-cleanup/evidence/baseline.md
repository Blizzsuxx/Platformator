# Baseline results

Source and build provenance: [baseline-provenance.md](baseline-provenance.md). The frozen source contains the staged user changes and common measurement fixture. Configured and built `cleanup-debug`, `production` and `cleanup-benchmark` in `/tmp/platformator-006-baseline/source` using CMake and Ninja. Full commands are in `quickstart.md`; configure/build logs are under `/tmp/platformator-006-baseline/`.

- Debug: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software ctest --test-dir bin/cleanup-debug --output-on-failure -L platformator`: 24 passed, two skipped (`runtime_work`, `benchmark_contract`); package/installed/relocated consumers and Mario smoke passed.
- Production: same drivers and `ctest --test-dir bin/production --output-on-failure -L platformator`: 24 passed, the same two skipped; package/installed/relocated consumers and Mario smoke passed.
- Benchmark: `ctest --test-dir bin/cleanup-benchmark --output-on-failure -L benchmark`: three passed.
- Baseline optional loads intentionally return fallback for missing texture/audio/animation as characterized by `asset_root_contract_test`; required scene asset failures remain separate. The candidate must intentionally change the former behavior.

Raw logs and status are in `/tmp/platformator-006-baseline/ctest-{debug,production,benchmark}.log`; source manifest is in `/tmp/platformator-006-baseline/manifest.sha256`. Public header targets were included in the complete debug and production builds. No candidate or visual/sanitizer claims are made here.
