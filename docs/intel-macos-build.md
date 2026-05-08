# Intel macOS build (unsupported / personal use)

Intel macOS is **not** in the v1.0 supported platform matrix (per
`CLAUDE.md` — Phase 0 dropped it from CI for runner-pool reasons). The
toolchain still produces a working binary; this document captures the
exact recipe so future sessions or workstations can reproduce it
without re-discovering the gotchas.

Treat this as a private-deployment path. Determinism, signing, and
notarisation are **not** validated for x86_64-mac. Don't ship the
resulting binary externally without going through the normal Phase 5
release flow.

## Build

From an Apple Silicon Mac (cross-compile) or natively on an Intel Mac:

```bash
rm -rf build-x86
cmake -B build-x86 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DSFS_INSTALL_PLUGIN_AFTER_BUILD=OFF \
  -DSFS_BUILD_TESTS=OFF \
  -DSFS_BUILD_TOOLS=OFF
cmake --build build-x86 --target sfs_plugin_VST3 --parallel
```

Required flags, with rationale:

| Flag | Why |
|---|---|
| `CMAKE_OSX_ARCHITECTURES=x86_64` | Forces single-arch x86_64 output. `cmake/SimdConfig.cmake` keys off this to attach `-mavx2` only to the x86_64 slice (via `-Xarch_x86_64` on Apple). |
| `CMAKE_OSX_DEPLOYMENT_TARGET=11.0` | C++20 `<semaphore>` (used by `src/engine/voice_pool.cpp`) was introduced in Apple's libc++ in macOS 11.0 / Big Sur. Older targets (10.15 / Catalina) fail to compile with `'release' is unavailable`. 11.0 covers every Intel Mac that ran a 2020+ macOS, which is the practical floor for any DAW-capable machine. |
| `SFS_INSTALL_PLUGIN_AFTER_BUILD=OFF` | Skips the post-build copy to `~/Library/Audio/Plug-Ins/VST3/`. We're cross-compiling for another machine, not the build host. |
| `SFS_BUILD_TESTS=OFF`, `SFS_BUILD_TOOLS=OFF` | Skips test/tool targets; the deployment artefact is just `SFS.vst3`. |

The output bundle:

```
build-x86/src/plugin/sfs_plugin_artefacts/Release/VST3/SFS.vst3
```

## Verify before transferring

```bash
lipo -archs build-x86/src/plugin/sfs_plugin_artefacts/Release/VST3/SFS.vst3/Contents/MacOS/SFS
# expect: x86_64

otool -l build-x86/src/plugin/sfs_plugin_artefacts/Release/VST3/SFS.vst3/Contents/MacOS/SFS \
  | grep -A 4 LC_BUILD_VERSION | head -6
# expect: minos 11.0
```

If `minos` shows the SDK version (e.g. 26.0) instead of 11.0, the
deployment-target flag wasn't honoured — re-check the cmake invocation
(stale `build-x86/` from a prior config; remove and re-configure).

## Install on the Intel target machine

Live and many other DAWs only scan the system-wide path on a default
configuration. Install there:

```bash
# On the Intel Mac, after copying SFS.vst3 over:
sudo rm -rf /Library/Audio/Plug-Ins/VST3/SFS.vst3
sudo cp -R SFS.vst3 /Library/Audio/Plug-Ins/VST3/

# Drop quarantine + ad-hoc sign so Gatekeeper doesn't block load
sudo xattr -cr /Library/Audio/Plug-Ins/VST3/SFS.vst3
sudo codesign --force --deep --sign - /Library/Audio/Plug-Ins/VST3/SFS.vst3

# CRITICAL: relax permissions so the DAW's user-mode scanner can read
# into the bundle. `sudo cp -R` copies with root-restrictive perms;
# without this step Live's scanner crashes on the bundle (shows up as
# a folder-with-red-stop icon in the plug-in browser).
sudo chmod -R go+rX /Library/Audio/Plug-Ins/VST3/SFS.vst3
```

Then full rescan in the host:

- **Live**: Settings → Plug-Ins → hold **⌥ Option** while clicking
  Rescan. (Plain rescan is incremental and skips known plugins,
  including ones blocklisted from a prior failed scan.)
- **Reaper**: Preferences → Plug-Ins → VST → Re-scan.
- **Cubase**: Studio → VST Plug-In Manager → Update.

## If the plug-in shows a red-stop / folder icon (Live)

That means Live's scanner crashed on instantiation. In order of
likelihood:

1. **Permission**: re-run `sudo chmod -R go+rX` and full rescan with
   ⌥ held.
2. **Stale bundle**: confirm `otool -l ... LC_BUILD_VERSION` reports
   `minos 11.0`, not the SDK version.
3. **OS too old**: `sw_vers -productVersion` < 11.0 means the target
   Mac can't run this binary; either update the OS or bump the
   deployment-target floor lower (and accept any compile errors —
   currently there are none below 11.0 except `<semaphore>`).
4. **Pre-AVX2 CPU**: very old Intel Macs (pre-Haswell, ~2012 and
   earlier) lack AVX2. Symptom is `EXC_BAD_INSTRUCTION` in
   `~/Library/Logs/DiagnosticReports/`. Rebuild with
   `-DSFS_BUILD_ECO=ON` to suppress the AVX2 path entirely (SSE2 only).
5. **Real plugin bug**: rare for x86_64-mac (CI exercises x86 SIMD on
   Linux + Windows). If suspected, run `pluginval` on the target Mac:
   ```
   /Applications/pluginval.app/Contents/MacOS/pluginval \
     --strictness-level 5 \
     --validate /Library/Audio/Plug-Ins/VST3/SFS.vst3
   ```

## Caveats

- **No code-signing identity** — the ad-hoc signature works for local
  use but won't pass notarisation. Don't redistribute.
- **Determinism not validated** — CI compares arm64-mac vs
  x86_64-{Linux, Windows}. The x86_64-mac binary uses the same x86 SIMD
  codepath as Linux/Windows so should produce identical samples in
  practice, but it's not enforced. Don't trust this build for
  collaborative work where bit-exact session reproduction matters.
- **CI does not protect this path** — a future engine change could
  break the x86_64-mac build (most commonly via deployment-target
  drift) without anyone noticing. Re-run this recipe end-to-end before
  each personal deployment.
