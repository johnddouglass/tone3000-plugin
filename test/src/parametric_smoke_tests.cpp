// [parametric] De-risk + wiring tests for parametric (.param.nam) support on the
// TONE3000 plugin fork.
//   1. Core-level: the mrgeneko NeuralAmpModelerCore fork loads an A2 parametric
//      model through the stock nam::DSP seam and FiLM knobs change the output.
//   2. NamEngine: setKnobValues() fans out to the model and moves the sound.
//   3. Processor: a local .param.nam drop is ACCEPTED by the (widened) A2 gate.
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NamEngine.h"
#include "Processor.h"
#include "test_helpers.h"

namespace {
constexpr const char* kParamModel = "large-muffin.param.nam";

std::filesystem::path paramModelPath() {
  return std::filesystem::path(T3K_TEST_FILES_DIR) / kParamModel;
}
double rmsOf(const std::vector<double>& v) {
  double s = 0.0;
  for (double x : v) s += x * x;
  return std::sqrt(s / static_cast<double>(v.size()));
}
// Fresh render of `in` at a given knob vector (core-level DSP).
void renderAt(nam::DSP& m, const std::vector<float>& knobs, const std::vector<double>& in,
              std::vector<double>& out) {
  const int N = static_cast<int>(in.size());
  m.Reset(48000.0, N);
  m.SetKnobValues(knobs);
  std::vector<double> inCopy = in;
  double* ip = inCopy.data();
  double* op = out.data();
  m.process(&ip, &op, N);
}
}  // namespace

// --- 1. Core-level (fork core through nam::DSP) --------------------------------

TEST(ParametricSmoke, LoadsA2ParametricModelThroughForkCore) {
  const auto path = paramModelPath();
  ASSERT_TRUE(std::filesystem::exists(path)) << "missing test asset: " << path;

  std::unique_ptr<nam::DSP> model = nam::get_dsp(path);
  ASSERT_NE(model, nullptr) << "get_dsp returned null for a SlimmableContainer/ParametricWaveNet";

  const int nParams = model->GetNumParams();
  std::cout << "[ParametricSmoke] GetNumParams() = " << nParams << "\n";
  ASSERT_GT(nParams, 0) << "fork core did not expose parametric knobs through nam::DSP*";

  const auto defs = model->GetParameterDefs();
  ASSERT_EQ(static_cast<int>(defs.size()), nParams);
  for (const auto& d : defs)
    std::cout << "  knob '" << d.name << "' range [" << d.min_val << ", " << d.max_val
              << "] default " << d.default_val << " steps " << d.steps << "\n";
}

TEST(ParametricSmoke, KnobValuesChangeTheOutput) {
  const auto path = paramModelPath();
  ASSERT_TRUE(std::filesystem::exists(path));
  std::unique_ptr<nam::DSP> model = nam::get_dsp(path);
  ASSERT_NE(model, nullptr);
  const int nParams = model->GetNumParams();
  ASSERT_GT(nParams, 0);

  const int N = 8192;
  std::vector<double> in(N);
  for (int i = 0; i < N; ++i) in[i] = 0.3 * std::sin(2.0 * M_PI * 196.0 * i / 48000.0);

  std::vector<double> outLow(N, 0.0), outHigh(N, 0.0);
  renderAt(*model, std::vector<float>(nParams, 0.0f), in, outLow);
  renderAt(*model, std::vector<float>(nParams, 1.0f), in, outHigh);

  double meanAbsDiff = 0.0;
  for (int i = 0; i < N; ++i) meanAbsDiff += std::abs(outLow[i] - outHigh[i]);
  meanAbsDiff /= N;
  std::cout << "[ParametricSmoke] rms(knobs@0)=" << rmsOf(outLow)
            << " rms(knobs@1)=" << rmsOf(outHigh) << " mean|delta|=" << meanAbsDiff << "\n";

  EXPECT_GT(meanAbsDiff, 1e-5) << "knob change did not alter output — FiLM conditioning inert";
}

// --- 2. NamEngine: knob fan-out drives the hosted model ------------------------

TEST(ParametricSmoke, NamEngineFansKnobValuesToTheModel) {
  const auto path = paramModelPath();
  ASSERT_TRUE(std::filesystem::exists(path));
  std::unique_ptr<nam::DSP> model = nam::get_dsp(path);
  ASSERT_NE(model, nullptr);
  const int nParams = model->GetNumParams();
  ASSERT_GT(nParams, 0);

  std::vector<std::unique_ptr<nam::DSP>> instances;
  instances.push_back(std::move(model));
  NamEngine engine(std::move(instances), /*oversampleFactor=*/1);

  EXPECT_EQ(engine.getNumParams(), nParams);
  EXPECT_EQ(static_cast<int>(engine.getParameterDefs().size()), nParams);

  const int N = 4096;
  auto render = [&](float knobVal, std::vector<float>& out) {
    juce::AudioBuffer<float> buf(1, N);
    float* d = buf.getWritePointer(0);
    for (int i = 0; i < N; ++i)
      d[i] = 0.3f * std::sin(2.0f * static_cast<float>(M_PI) * 196.0f * i / 48000.0f);
    engine.setKnobValues(std::vector<float>(nParams, knobVal));
    engine.prepare(N);  // re-asserts the knob values just set, from a clean reset
    engine.process(buf, nullptr);
    out.assign(d, d + N);
  };

  std::vector<float> lo, hi;
  render(0.0f, lo);
  render(1.0f, hi);

  double meanAbsDiff = 0.0;
  for (int i = 0; i < N; ++i) meanAbsDiff += std::abs(lo[i] - hi[i]);
  meanAbsDiff /= N;
  std::cout << "[ParametricSmoke] NamEngine mean|delta| = " << meanAbsDiff << "\n";
  EXPECT_GT(meanAbsDiff, 1e-5f) << "NamEngine::setKnobValues did not reach the model";
}

// --- 3. Processor: the widened A2 gate accepts a local parametric drop ---------

TEST(ParametricSmoke, ProcessorGateAcceptsLocalParametricModel) {
  TONE3000Processor proc;
  proc.prepareToPlay(48000.0, 512);

  const juce::var res = proc.loadLocalTonePath(testFile(kParamModel));
  ASSERT_TRUE(res.isObject()) << "loadLocalTonePath returned a non-object";
  // Before widening namConfigIsA2, a ParametricWavenet SlimmableContainer was
  // rejected as "not A2"; it should now load and yield a block.
  EXPECT_FALSE(res.hasProperty("error"))
      << "parametric model rejected: " << res.getProperty("error", "").toString();
  EXPECT_TRUE(res["blockId"].toString().isNotEmpty()) << "no block created for parametric model";
}
