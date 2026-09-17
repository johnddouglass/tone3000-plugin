// [parametric] Processor-level tests for parametric knob control: a parametric
// .param.nam loaded through the real chain exposes its knobs to getChainState,
// and setBlockParametricKnobs moves the chain's audio output. Uses the same
// cache-first restoreFromTree rig as the other chain tests (synchronous load).
#include "chain_test_helpers.h"

#include <cmath>
#include <vector>

namespace {
constexpr int kBlk = 512;
constexpr int kWarm = 15;  // settle smoothers / load-in fade before measuring
constexpr int kRun = 20;

juce::ValueTree monoParamChain(const juce::String& blockId) {
  juce::ValueTree state("ChainSnapshot");
  state.setProperty("stereoEnabled", false, nullptr);
  juce::ValueTree lane("ChainBlocks");
  lane.appendChild(makeNamBlockTree(blockId, 1, 100, "large-muffin.param.nam"), nullptr);
  state.appendChild(lane, nullptr);
  state.appendChild(juce::ValueTree("RightChainBlocks"), nullptr);
  return state;
}
}  // namespace

TEST(ParametricProcessor, KnobsChangeAudioThroughTheChain) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlk);
  proc.prepareToPlay(kFs, kBlk);
  proc.restoreFromTree(monoParamChain("blk-p"));
  ASSERT_TRUE(waitForChainLoaded(proc)) << "parametric model never finished loading from cache";

  // The loaded model's knobs are surfaced to the UI via getChainState.
  EXPECT_TRUE(juce::JSON::toString(proc.getChainState(-1)).contains("parametricKnobs"))
      << "getChainState did not expose parametricKnobs";

  // Same noise seed each run, so the only difference is the knob setting.
  auto runAtKnobs = [&](std::vector<float> knobs) {
    EXPECT_TRUE(proc.setBlockParametricKnobs("blk-p", knobs));
    letAudioGoIdle();
    processStereo(proc, makeNoise(kWarm * kBlk, 1111, 0.25f));  // discard: settle
    return processStereo(proc, makeNoise(kRun * kBlk, 4242, 0.25f)).first;
  };

  const auto low = runAtKnobs({0.0f, 0.0f});
  const auto high = runAtKnobs({1.0f, 1.0f});

  ASSERT_EQ(low.size(), high.size());
  double meanAbsDiff = 0.0;
  for (size_t i = 0; i < low.size(); ++i) meanAbsDiff += std::abs(low[i] - high[i]);
  meanAbsDiff /= static_cast<double>(low.size());
  std::cout << "[ParametricProcessor] mean|delta| = " << meanAbsDiff << "\n";
  EXPECT_GT(meanAbsDiff, 1e-4) << "setBlockParametricKnobs did not alter the chain output";
}

TEST(ParametricProcessor, RejectsMismatchedKnobCount) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlk);
  proc.prepareToPlay(kFs, kBlk);
  proc.restoreFromTree(monoParamChain("blk-p"));
  ASSERT_TRUE(waitForChainLoaded(proc));

  EXPECT_FALSE(proc.setBlockParametricKnobs("blk-p", {0.5f}));                // too few
  EXPECT_FALSE(proc.setBlockParametricKnobs("blk-p", {0.5f, 0.5f, 0.5f}));    // too many
  EXPECT_FALSE(proc.setBlockParametricKnobs("no-such-block", {0.5f, 0.5f}));  // missing block
  EXPECT_TRUE(proc.setBlockParametricKnobs("blk-p", {0.5f, 0.5f}));           // exact count
}
