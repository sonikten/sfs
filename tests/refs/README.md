# tests/refs/

Reference hashes and (where small enough) reference WAV/raw renders that
the determinism CI gate compares against on every push.

## How to verify locally

```
cmake --build build --target sfs_render
tools/render_reference.sh build/ref_renders
( cd build/ref_renders && shasum -a 256 -c sine_skeleton_48k_256.raw \
    | diff - <(cat ../../tests/refs/sine_skeleton_48k_256.sha256) )
```

Or shorter, using the committed `.sha256` file directly:

```
( cd build/ref_renders \
    && cp ../../tests/refs/sine_skeleton_48k_256.sha256 . \
    && shasum -a 256 -c sine_skeleton_48k_256.sha256 )
```

## How to update a reference hash (when an intentional acceptance change lands)

1. Make sure the engine change is intentional and the new sound is what you want.
2. Re-render via `tools/render_reference.sh` on **macOS arm64**, **Windows x86_64**, and **Linux x86_64** (CI will do this).
3. Confirm all three platforms agree on the new hash (run the determinism CI workflow on the change).
4. Replace the hash in this directory's `.sha256` file.
5. Commit the new hash *with* the engine change in the same PR. The commit message MUST explain why the audio output changed — this is a sonic acceptance change, not a bug.

A change to a reference hash without an explanation in the commit message is a Phase-0-gate-style review fail.

## Inventory

| File | Source | Spec | Notes |
|---|---|---|---|
| `sine_skeleton_48k_256.sha256` | `sfs_render --sr 48000 --block 256 --seconds 1.0 --channels 2 --raw` with no MIDI | `sfs-spec/06 §2.2` | Now silence (Phase 1 plug-in plays nothing without MIDI). Hash is the SHA-256 of 384000 zero bytes; mismatching means the engine is leaking something into its output without a gate-on. |
| `phase1_canonical_c4_48k_256.sha256` | `sfs_render … --note 60 --velocity 1.0` (default gate-off at duration/2) | sfs-spec/02 + 03 + 04 | The Phase 1 canonical engine render: substrate at default coefficients (c²=0.30, κ=0.05, γ=0.005), 16 sine agents at evenly spaced positions, mono harvester at substrate midpoint, MIDI C4 at velocity 1.0, 0.5 s sustain + 0.5 s decay tail. This is the audible Phase 1 acceptance render. |

Phase 1 will add reference hashes for the sonic-corner contract presets
(drone-degraded, pitched) when the contract harness lands.
