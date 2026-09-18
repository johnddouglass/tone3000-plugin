// [link] Stereo chain-link core: enabling the link mirrors the Left lane onto
// the Right, so identical input on both channels renders identical L/R output
// (dual-mono stereo). Uses the cache-first restoreFromTree rig.
#include "chain_test_helpers.h"

#include <algorithm>
#include <cmath>

namespace {
juce::ValueTree stereoLeftOnlyChain() {
  juce::ValueTree state("ChainSnapshot");
  state.setProperty("stereoEnabled", true, nullptr);
  juce::ValueTree left("ChainBlocks");
  left.appendChild(makeNamBlockTree("blk-L", 1, 100, "a2-amp-test.nam"), nullptr);
  state.appendChild(left, nullptr);
  state.appendChild(juce::ValueTree("RightChainBlocks"), nullptr);  // empty right lane
  return state;
}

double sumAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
  double d = 0.0;
  for (size_t i = 0; i < a.size(); ++i) d += std::abs(a[i] - b[i]);
  return d;
}
double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
  double m = 0.0;
  for (size_t i = 0; i < a.size(); ++i) m = std::max(m, static_cast<double>(std::abs(a[i] - b[i])));
  return m;
}
}  // namespace

TEST(ChainLink, LinkMirrorsLeftOntoRight) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, 512);
  proc.prepareToPlay(kFs, 512);
  proc.restoreFromTree(stereoLeftOnlyChain());
  ASSERT_TRUE(waitForChainLoaded(proc));
  ASSERT_TRUE(proc.isStereoMode());
  EXPECT_FALSE(proc.areChainsLinked());

  // Sanity: unlinked, only the Left lane has a model, so L and R differ.
  {
    const auto out = processStereo(proc, makeNoise(20 * 512, 7, 0.25f));
    EXPECT_GT(sumAbsDiff(out.first, out.second), 1e-3)
        << "unlinked L/R should differ (only Left has a model)";
  }

  // Link -> Right becomes an exact mirror of Left.
  ASSERT_TRUE(proc.setChainsLinked(true));
  EXPECT_TRUE(proc.areChainsLinked());
  ASSERT_TRUE(waitForChainLoaded(proc)) << "right mirror never finished its cache-first load";
  letAudioGoIdle();

  // Identical chains + identical per-channel input -> identical L/R output.
  // The two lanes are separate engine instances; run a long settle window so
  // their internal (ring-buffer) states fully converge from the fresh-mirror
  // start, then compare. -80 dBFS is unambiguously "the same chain" vs the huge
  // unlinked difference above.
  processStereo(proc, makeNoise(60 * 512, 3, 0.25f));  // settle both engines' state
  const auto out = processStereo(proc, makeNoise(20 * 512, 9, 0.25f));
  const double m = maxAbsDiff(out.first, out.second);
  std::cout << "[ChainLink] max|L-R| after link = " << m << "\n";
  EXPECT_LT(m, 1e-4) << "linked chains should render L and R identically";
}

TEST(ChainLink, ForwardsParamEditsToRight) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, 512);
  proc.prepareToPlay(kFs, 512);
  proc.restoreFromTree(stereoLeftOnlyChain());
  ASSERT_TRUE(waitForChainLoaded(proc));
  ASSERT_TRUE(proc.setChainsLinked(true));
  ASSERT_TRUE(waitForChainLoaded(proc));

  // Change the Left block's output gain. While linked this must be forwarded to
  // the Right twin, so identical input still renders identical L/R.
  EXPECT_TRUE(proc.setBlockParam("blk-L", "outputGain", 0.8));
  letAudioGoIdle();
  processStereo(proc, makeNoise(60 * 512, 3, 0.25f));  // settle
  const auto out = processStereo(proc, makeNoise(20 * 512, 9, 0.25f));
  const double m = maxAbsDiff(out.first, out.second);
  std::cout << "[ChainLink] max|L-R| after Left param edit = " << m << "\n";
  EXPECT_LT(m, 1e-4) << "a param edit on Left should be mirrored to Right";
}

TEST(ChainLink, LinkRejectedOutsideStereo) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, 512);
  proc.prepareToPlay(kFs, 512);
  // Mono chain (stereo off) — link is a no-op.
  juce::ValueTree state("ChainSnapshot");
  state.setProperty("stereoEnabled", false, nullptr);
  juce::ValueTree left("ChainBlocks");
  left.appendChild(makeNamBlockTree("blk-L", 1, 100, "a2-amp-test.nam"), nullptr);
  state.appendChild(left, nullptr);
  state.appendChild(juce::ValueTree("RightChainBlocks"), nullptr);
  proc.restoreFromTree(state);
  ASSERT_TRUE(waitForChainLoaded(proc));
  ASSERT_FALSE(proc.isStereoMode());
  EXPECT_FALSE(proc.setChainsLinked(true));
  EXPECT_FALSE(proc.areChainsLinked());
}
