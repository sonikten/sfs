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

| File | Bytes | Source | Spec |
|---|---|---|---|
| `sine_skeleton_48k_256.sha256` | 1 hash | `sfs_render --sr 48000 --block 256 --seconds 1.0 --channels 2 --raw` of the Phase 0 skeleton AudioProcessor (continuous polynomial 440 Hz sine, ScopedNoDenormals, 7th-order Hastings dm_sin) | `sfs-spec/06 §2.2` |

Phase 1 will add reference hashes for the 1D-substrate canonical contract presets (drone-degraded, pitched).
