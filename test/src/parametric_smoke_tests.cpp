// [parametric] De-risk smoke test: proves the mrgeneko NeuralAmpModelerCore fork
// loads an A2 parametric (.param.nam) model through the stock nam::DSP seam and
// that FiLM knob values actually change the output. Pure core-level (bypasses the
// plugin's local-file gate) — the gate widening + NamEngine wiring come later.
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

namespace {
std::filesystem::path paramTestFile(const char* n) {
  return std::filesystem::path(T3K_TEST_FILES_DIR) / n;
}
double rmsOf(const std::vector<double>& v) {
  double s = 0.0;
  for (double x : v) s += x * x;
  return std::sqrt(s / static_cast<double>(v.size()));
}
// Fresh render of `in` at a given knob vector. process() may touch the input
// buffer, so hand it a private copy.
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

TEST(ParametricSmoke, LoadsA2ParametricModelThroughForkCore) {
  const auto path = paramTestFile("large-muffin.param.nam");
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
  const auto path = paramTestFile("large-muffin.param.nam");
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
