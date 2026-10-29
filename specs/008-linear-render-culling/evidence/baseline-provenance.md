# T044 Source and Binary Provenance

## Pinned pre-feature source and common support

- Pinned commit: `ad9b4f38188dd7e54ba6ceb46263dfe22f349b75`.
- Git tree: `ab35d6670258b8b1ab790b9448b82ae34a4e22f9`.
- Retained source archive: `benchmark/artifacts/linear-render-culling/20261029-t044-run-01/baseline/source/pinned-ad9b4f38188dd7e54ba6ceb46263dfe22f349b75.tar`; SHA-256 `c2cd5e0163510fe85cf041100ece28b5b7be4a9cc170777abcf685473f5ad953`.
- Baseline aggregate source identity: `536cd40de0dea634250c56ffca20e68231ded659cef7257c9b8b9d60ee42529f`, independently rehashed across 131 CMake fingerprint inputs. Input manifest SHA-256: `8b8cc2046e6596d580006a00468276c0ae2e1c5cf519adc3b497879e7088966c`.
- Candidate source identity: `4a5ce52e10fc81f903c3687879ee9d883836ec13668e18185f967fc6fa20dfba`, independently rehashed across 132 inputs. Input manifest SHA-256: `9d1d58700549c59511313c5ee016d3be7557d7ac72464afd36b8874590090dc7`.
- Candidate source archive SHA-256: `d8c100e7d7595a0cf15e88d23c29f36e1fcc14025c6e15714b4c707c653e58a7`.
- Shared fixture: `caff033f8a7577eb7d3f9eafe5a897bb09a70db78b385a7d010d4c174d84e65d`; common harness: `83f7119f120a7a3fd401028553ee29595b23e93a87f101b58ecb3611bc1abbd7`.

The baseline archive was exported from the pinned Git commit, then received only the finalized common fixture/probe/build fingerprint and work-probe observation support. `provenance/baseline-common-support.patch` is the exact unified diff; SHA-256 `e5dea322123e52f16b501ad6dae9ecdde7e7c429812185b6a327bbc2cb12375d`. Its nine changed paths are `CMakeLists.txt`, `benchmark/capture_linear_render.py`, `benchmark/linear_render_probe.cpp`, `src/benchmarkbuildconfig.h.in`, `src/camera.cpp`, `src/sdlwindow.cpp`, `src/workprobe.h`, `tests/render_culling_fixture.h`, and `tests/runtime_work_probe.h`. The baseline camera still submits every sprite; its only draw-path addition is the compile-time-gated observation callback. The pinned settings and camera headers contain no culling setting, view preparation, mapping query, or sprite filter. The audit verifies this and confirms no candidate culling implementation was copied.

The per-input audit is `provenance/source-manifest-audit.json` (SHA-256 `88b2549deab186eccffd0f397d33bd3c56fcdbd0f8f78665ddc3ec91e56d3fad`). `provenance/binary-identities.json` (SHA-256 `2bf4def7dbab76755a8ca87c120c80e34cb22f2b2e58cc1d7a64f69da4139fec`) records build options and SHA-256 for each focused probe and stock runner.

## Separate builds and binary identities

All builds used CMake 4.4.4, Ninja 1.13.2, GCC/G++ 16.2.1, C++20, bundled SDL 3.4.18, SDL_image 3.4.8, SDL_ttf 3.2.2, SDL_mixer 3.2.4, Eigen 5.0.1, oneTBB 2022.3.0, Release `-O3 -DNDEBUG`, debug tools off, and sanitizer `none`. Timing builds have work probes off; validation builds have them on. Candidate-disabled and candidate-linear timing share one candidate timing binary.

| Binary | SHA-256 |
|---|---|
| Baseline timing probe | `429346bee7e0fbde09ed3500f2e0780146cc1f570106ba19df3a156a6e329258` |
| Candidate timing probe | `96689171521df58a49aaf601251fdf3090e7ba698cc1816440b34b9c9e3228da` |
| Baseline validation probe | `b69bc755d4e1f9ee4a60bb26b1950cd813f46687347e616e5e56221a6d4ba6c5` |
| Candidate validation probe | `4ef4555d47b5aa639c19c8d75f65f13f7ac0cf3dac8e0957a6348395744512a8` |
| Baseline timing stock runner | `7038234650e54a91b031b50c8f118dce9d39931fc223f08431016e1208ebb46f` |
| Candidate timing stock runner | `3ac1f301678c25b8c9387715eed375c0b8c720b68917a9f9f557bf62dfa175ce` |
| Baseline validation stock runner | `61d4cf0ef60cc708f81fb053c864820b0830470da48351b40d82732aecfb6c36` |
| Candidate validation stock runner | `65f3141aa361f538a66bf6850ab5e0ef5a7e84864246153e15821174bb8b3045` |

`nm -C --defined-only` output is retained for both timing probes. Neither defines the validation allocation observer or replacement global `operator new` symbols. The complete build logs, CMake caches, compile commands, generated source manifests, and binaries are under the ignored evidence bundle.

## Historical provenance notes

Earlier Markdown summaries cited a different baseline identity and non-retrievable `/tmp` trees. The old raw evidence was absent during T044 recovery; the old summary files are preserved as explicitly superseded snapshots under `provenance/superseded-summaries/`. The new source archives, per-input manifests, common patch, build identities, and timing captures are independent and are the only current acceptance evidence. No branch switch or candidate engine edit was made for this recapture.
