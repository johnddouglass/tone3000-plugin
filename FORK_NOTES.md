# Fork notes — parametric NAM on the TONE3000 plugin

This is a fork of **tone-3000/tone3000-plugin** that adds **parametric (FiLM /
`.param.nam`) model support** while keeping oversampling, multicore, IR islands
and the rest of the upstream plugin intact.

## Repo layout
- **origin**   `johnddouglass/tone3000-plugin`  (this fork)
- **upstream** `tone-3000/tone3000-plugin`       (track this for updates)
- Core submodule `plugin/NeuralAmpModelerCore` → **`johnddouglass/NeuralAmpModelerCore`**
  (a fork of `mrgeneko/NeuralAmpModelerCore`, which adds `ParametricWaveNet` + FiLM +
  the polymorphic knob API on top of stock `sdatkinson/NeuralAmpModelerCore`).
  Pinned at `419bb57` ("Remove LoRA support").

## Why the core is a full fork, not a patch
mrgeneko's parametric core is NOT an additive layer — the parametric/A2 work is
entangled across many shared files (`a2_fast.cpp`, `container.cpp`, `film.h`,
`linear.cpp`, …), so we adopt it wholesale rather than cherry-pick.

## The clean seam
Upstream `NamEngine` holds `std::vector<std::unique_ptr<nam::DSP>>` and only uses
the polymorphic base, which the fork extends with:
- `virtual void SetKnobValues(const std::vector<float>&)`  (thread-safe)
- `virtual int GetNumParams() const`
- `virtual std::vector<DSPParamDef> GetParameterDefs() const`
  where `DSPParamDef { name, min_val, max_val, default_val, steps }`
  (`steps` 0/1 = continuous knob, ≥2 = segmented switch).
So parametric models flow through the existing engine (and the oversampler /
phase-interleaving — they're phase-safe) with no core patching.

## Our changes to upstream files (keep these small + `// [parametric]`-marked)
- `.gitmodules` — core submodule URL → our core fork.
- `plugin/CMakeLists.txt` — add `NeuralAmpModelerCore/NAM/wavenet/parametric_wavenet.cpp` to `NAM_SRC`.
- `test/CMakeLists.txt` + `test/src/parametric_smoke_tests.cpp` + `test/files/large-muffin.param.nam`
  — de-risk regression test (loads a `.param.nam`, checks knobs, asserts output changes).

### Planned (not yet done)
- `plugin/src/ProcessorModelLoader.cpp` — widen `namConfigIsA2` + `namConfigIsPhaseSafe`
  to admit `"ParametricWaveNet"` so local `.param.nam` drops pass the gate.
- `plugin/src/NamEngine.cpp` — read `GetParameterDefs()` on load; `SetKnobValues()`
  per block on all N phase instances.
- `plugin/src/Processor*.cpp` — register dynamic JUCE params from the metadata.
- `ui/` (React/WebView) — dynamic knob/switch rendering + `JuceBackend` bridge (the bulk).

New logic should live in NEW files where possible; edits to upstream files stay as
small, localized, `// [parametric]`-tagged hunks to keep merges trivial.

## Updating from upstream
```bash
git fetch upstream
git merge upstream/main          # or rebase; conflicts should only touch the marked hunks
git submodule update --init --recursive
./script/test-dsp.sh             # DspTests must stay green (156 upstream + ParametricSmoke)
```

## Building this fork's plugin
The UI builds into `plugin/webview` (a configure-time GLOB), so build the UI first,
reconfigure, then the plugin. `-DT3K_PARAM_BUILD=ON` gives it a distinct name
("TONE3000 Param") + PLUGIN_CODE (own VST3/AU UID) so it coexists with an official
TONE3000 install instead of colliding:
```bash
cd ui && npm run build            # -> plugin/webview  (npm cache: --cache <writable> if ~/.npm is root-owned)
cd .. && cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_OSX_ARCHITECTURES=arm64 -DT3K_PARAM_BUILD=ON
cmake --build build --target TONE3000_VST3 -j8
# bundle: build/plugin/TONE3000_artefacts/Release/VST3/TONE3000 Param.vst3
```

## Core sync (our own duty)
If upstream bumps its stock core to a newer version, reconcile it in the core fork
(rebase the parametric changes onto newer stock), then bump this submodule. Re-baseline
the oversampling/aliasing tests if `a2_fast` numerics shift.

## De-risk status (2026-09-17)
Core swap builds clean; **all 156 upstream tests pass + 2 ParametricSmoke tests pass**
(`large-muffin.param.nam` loads via `get_dsp`, exposes Sustain/Tone, output responds
to `SetKnobValues`). DSP path proven; only plugin-wiring + UI remain.
