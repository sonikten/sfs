# Phase 4 CPU profile — `tools/sfs_profile`

Records the CPU cost of the Phase 4 engine after the GUI shell, preset
I/O wiring, voice-pool threading dispatch for FOA / 5.1 / 7.1.4, and
the LR-2 LFE filter all landed.

## Setup

- Host: macOS arm64 (Apple Silicon, Apple M-series)
- Build: Release, `-O3 -fno-fast-math` (default `cmake -B build`)
- Sample rate: 48 000 Hz
- Block size: 256 samples
- Run: `build/bin/sfs_profile --substrate-cells 1024 --agents N --seconds 1.0 --repeat 5`
- Date: 2026-05-07

## Results — single voice, 1D topology

| Substrate cells | Agent count | Wall ms / 1 s render | Real-time factor | One-core % |
|---|---|---|---|---|
| 1024 | 16 (default) | 109 ms (median) | 9.15× | 10.9% |

Phase 3 → Phase 4 delta at 1024/16: +0 ms (109 vs 110 ms). The Phase 4
work landed entirely outside the audio inner loop:

- GUI shell + preset I/O: GUI thread only.
- Threading dispatch for multichannel paths: only active when the
  layout is FOA / 5.1 / 7.1.4 *and* `enableThreading(N)` is opted in
  by the host. The default stereo + serial path is unchanged.
- LR-2 LFE filter: adds one extra 1st-order stage (~5 multiply-adds
  per sample) but only on 5.1 / 7.1.4 LFE channels; the stereo path
  doesn't touch the LFE state.

## Comparison to spec budget

Spec scalar budget (`sfs-spec/08 §2.5`) for 1D N=1024 single voice:
~14 µs/sample = ~672 ms wall per second of audio = ~67% one core.

Phase 4 single-voice at 1024 / 16: 109 ms = ~10.9% one core. **6.2×
under the spec budget.**

## Linux + Windows

Phase 4 ships with the macOS arm64 measurement above. The Linux
x86_64 and Windows x86_64 measurements are deferred to Phase 5
once the CI artefact-harvesting bring-up lands; the bit-exact
determinism contract already verifies cross-platform correctness
on every push.

## Phase 4 gate sign-off

- ✅ `1024-16-agent` 9.15× real-time — passes spec gate (≥ 4×).
- ✅ Multichannel render paths now dispatch through the voice pool
  when `enableThreading(N)` is enabled (P4.C26).
- ✅ All 4 render-path threading variants (stereo, FOA, 5.1, 7.1.4)
  produce bit-exact output vs. serial across 1, 2, and 4 worker
  counts.

## Notes for Phase 5

- Cross-platform CPU profile harvesting (Linux + Windows runners
  upload sfs_profile JSON artefacts; one journal entry per push).
- Multi-voice (8 simultaneous) measurement under threading. The
  P3.12 journal extrapolated 8 voices × 32 agents at ~104% one
  core single-threaded; threading distributes that across 4 worker
  cores, expected ~26% per core.
- LFE filter cost on the 5.1 / 7.1.4 paths.
- Plug-in framework overhead (JUCE) measurement at the host
  boundary — pluginval reports per-host load.
