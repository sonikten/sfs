# Phase 3 CPU profile — `tools/sfs_profile`

Records the CPU cost of the Phase 3 engine after Cody-Waite dm_sin
(P3.1), 5 ms voice-steal ramp (P3.2), Substrate2D class (P3.3), Voice
2D mode (P3.4), and TOPOLOGY host parameter (P3.5).

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
| 1024 | 16 (Phase 2/3 default) | 110 ms (median) | 9.0× | 11.1% |
| 1024 | 32                     | 130 ms          | 7.7× | 13.0% |
| 1024 | 64 (sfs-spec/09 max)   | 148 ms          | 6.8× | 14.8% |

## Phase 2 → Phase 3 delta

| Configuration | Phase 2 ms | Phase 3 ms | Δ |
|---|---|---|---|
| 1024 / 16 |  70 | 110 | +57% |
| 1024 / 32 |  97 | 130 | +34% |
| 1024 / 64 | 113 | 148 | +31% |

The +50% at 16 agents is mostly from `dm_sin` (Phase 2 was 7th-order
Hastings in float; Phase 3 is 11th-order Taylor in double + Cody-Waite
range reduction). At higher agent counts the per-sample substrate
update dominates so the dm_sin cost amortises.

The Substrate2D member adds ~32×32 floats × 4 buffers = 16 KB per
voice, init cost only — render cost is unchanged in 1D mode (2D
substrate is unread).

The 5 ms voice-steal ramp adds one comparison + one fmin per sample
per channel (negligible).

## Comparison to `sfs-spec/08 §2.5` budget

Spec scalar budget for 1D N=1024: ~14 µs/sample = ~672 ms wall per
second of audio = ~67% one core for a single voice with full agent
load.

Phase 3 worst case (64 agents): 148 ms = ~15% one core. **4.5× under
the spec budget** — Cody-Waite + Taylor stays comfortably within
budget.

## Polyphony estimate (extrapolated)

| Voices | Agents/voice | Estimated one-core % |
|---|---|---|
| 8 | 16 | ~88% |
| 8 | 32 | ~104% (over budget — needs voice-pool threading)|
| 8 | 64 | ~118% (likewise) |

Phase 3 step 9 (voice-pool threading) is the planned mitigation.
Distributing 8 voices across 4 worker threads on a typical 4-core
host drops the per-core load by ~4×, comfortably under budget at
all configurations.

## 2D topology profile

`sfs_profile` doesn't yet support `--topology 2d`. The 2D substrate's
5-point Laplacian processes 2× the cells per cycle compared to the 1D
3-point stencil at the same total cell count (1024 = 32×32 = same as
1D 1024 ring), but the substrate update has 50% more memory traffic
because it accesses 4 neighbours instead of 2.

Expected 2D cost: ~1.3-1.5× the 1D cost at the same agent count.
Adding `--topology 2d` to sfs_profile is a Phase 3 follow-up.

## Phase 3 partial gate

- ✅ `1D-16-agent` 9.0× real-time — passes spec gate threshold (≥ 4×).
- ✅ `1D-32-agent` 7.7× real-time — passes.
- ✅ `1D-64-agent` 6.8× real-time — passes.

Single-voice rendering stays comfortably above the spec gate. Multi-voice
needs threading (step 9) — explicit follow-up.

## Notes for Phase 3 step 9 (threading)

- Profile multi-voice (8 simultaneous) once the threading lands.
- Profile on Linux + Windows runners (CI artifact harvesting).
- Verify bit-exact determinism with 1 / 2 / 4 / 8 worker counts —
  the per-voice render is already isolated; the bus mix is the only
  ordering hazard.
