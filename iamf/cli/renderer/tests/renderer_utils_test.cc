/*
 * Copyright (c) 2024, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at www.aomedia.org/license/software-license/bsd-3-c-c. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * www.aomedia.org/license/patent.
 */

#include "iamf/cli/renderer/renderer_utils.h"

#include <cstdint>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "iamf/cli/demixing_module.h"
#include "iamf/obu/audio_element.h"
#include "iamf/obu/mix_presentation.h"

namespace iamf_tools {
namespace renderer_utils {
namespace {

TEST(ArrangeSamplesToRender, SucceedsOnEmptyFrame) {
  std::vector<std::vector<int32_t>> samples;
  EXPECT_TRUE(ArrangeSamplesToRender({}, {}, samples).ok());
  EXPECT_TRUE(samples.empty());
}

TEST(ArrangeSamplesToRender, ArrangesSamplesInTimeChannelAxes) {
  const LabeledFrame kStereoLabeledFrame = {
      .label_to_samples = {{"L2", {0, 1, 2}}, {"R2", {10, 11, 12}}}};
  const std::vector<std::string> kStereoArrangement = {"L2", "R2"};

  std::vector<std::vector<int32_t>> samples;
  EXPECT_TRUE(
      ArrangeSamplesToRender(kStereoLabeledFrame, kStereoArrangement, samples)
          .ok());

  EXPECT_EQ(samples,
            std::vector<std::vector<int32_t>>({{0, 10}, {1, 11}, {2, 12}}));
}

TEST(ArrangeSamplesToRender, FindsDemixedLabels) {
  const LabeledFrame kDemixedTwoLayerStereoFrame = {
      .label_to_samples = {{"M", {75}}, {"L2", {50}}, {"D_R2", {100}}}};
  const std::vector<std::string> kStereoArrangement = {"L2", "R2"};

  std::vector<std::vector<int32_t>> samples;
  EXPECT_TRUE(ArrangeSamplesToRender(kDemixedTwoLayerStereoFrame,
                                     kStereoArrangement, samples)
                  .ok());

  EXPECT_EQ(samples, std::vector<std::vector<int32_t>>({{50, 100}}));
}

TEST(ArrangeSamplesToRender, IgnoresExtraLabels) {
  const LabeledFrame kStereoLabeledFrameWithExtraLabel = {
      .label_to_samples = {{"L2", {0}}, {"R2", {10}}, {"LFE", {999}}}};
  const std::vector<std::string> kStereoArrangement = {"L2", "R2"};

  std::vector<std::vector<int32_t>> samples;
  EXPECT_TRUE(ArrangeSamplesToRender(kStereoLabeledFrameWithExtraLabel,
                                     kStereoArrangement, samples)
                  .ok());
  EXPECT_EQ(samples, std::vector<std::vector<int32_t>>({{0, 10}}));
}

TEST(ArrangeSamplesToRender, LeavesEmptyLabelsZero) {
  const LabeledFrame kMixedFirstOrderAmbisonicsFrame = {
      .label_to_samples = {
          {"A0", {1, 2}}, {"A2", {201, 202}}, {"A3", {301, 302}}}};
  const std::vector<std::string> kMixedFirstOrderAmbisonicsArrangement = {
      "A0", "", "A2", "A3"};

  std::vector<std::vector<int32_t>> samples;
  EXPECT_TRUE(ArrangeSamplesToRender(kMixedFirstOrderAmbisonicsFrame,
                                     kMixedFirstOrderAmbisonicsArrangement,
                                     samples)
                  .ok());
  EXPECT_EQ(samples, std::vector<std::vector<int32_t>>(
                         {{1, 0, 201, 301}, {2, 0, 202, 302}}));
}

TEST(ArrangeSamplesToRender, ExcludesSamplesToBeTrimmed) {
  const LabeledFrame kMonoLabeledFrameWithSamplesToTrim = {
      .samples_to_trim_at_end = 2,
      .samples_to_trim_at_start = 1,
      .label_to_samples = {{"M", {999, 100, 999, 999}}}};
  const std::vector<std::string> kMonoArrangement = {"M"};

  std::vector<std::vector<int32_t>> samples;
  EXPECT_TRUE(ArrangeSamplesToRender(kMonoLabeledFrameWithSamplesToTrim,
                                     kMonoArrangement, samples)
                  .ok());
  EXPECT_EQ(samples, std::vector<std::vector<int32_t>>({{100}}));
}

TEST(ArrangeSamplesToRender, ClearsInputVector) {
  const LabeledFrame kMonoLabeledFrame = {.label_to_samples = {{"M", {1, 2}}}};
  const std::vector<std::string> kMonoArrangement = {"M"};

  std::vector<std::vector<int32_t>> samples = {{999, 999}};
  EXPECT_TRUE(
      ArrangeSamplesToRender(kMonoLabeledFrame, kMonoArrangement, samples)
          .ok());
  EXPECT_EQ(samples, std::vector<std::vector<int32_t>>({{1}, {2}}));
}

TEST(ArrangeSamplesToRender, TrimmingAllFramesFromStartIsResultsInEmptyOutput) {
  const LabeledFrame kMonoLabeledFrameWithSamplesToTrim = {
      .samples_to_trim_at_end = 0,
      .samples_to_trim_at_start = 4,
      .label_to_samples = {{"M", {999, 999, 999, 999}}}};
  const std::vector<std::string> kMonoArrangement = {"M"};

  std::vector<std::vector<int32_t>> samples;
  EXPECT_TRUE(ArrangeSamplesToRender(kMonoLabeledFrameWithSamplesToTrim,
                                     kMonoArrangement, samples)
                  .ok());
  EXPECT_TRUE(samples.empty());
}

TEST(ArrangeSamplesToRender,
     InvalidWhenRequestedLabelsHaveDifferentNumberOfSamples) {
  const LabeledFrame kStereoLabeledFrameWithMissingSample = {
      .label_to_samples = {{"L2", {0, 1}}, {"R2", {10}}}};
  const std::vector<std::string> kStereoArrangement = {"L2", "R2"};

  std::vector<std::vector<int32_t>> samples;
  EXPECT_FALSE(ArrangeSamplesToRender(kStereoLabeledFrameWithMissingSample,
                                      kStereoArrangement, samples)
                   .ok());
}

TEST(ArrangeSamplesToRender, InvalidWhenTrimIsImplausible) {
  const LabeledFrame kFrameWithExcessSamplesTrimmed = {
      .samples_to_trim_at_end = 1,
      .samples_to_trim_at_start = 2,
      .label_to_samples = {{"L2", {0, 1}}, {"R2", {10, 11}}}};
  const std::vector<std::string> kStereoArrangement = {"L2", "R2"};

  std::vector<std::vector<int32_t>> samples;
  EXPECT_FALSE(ArrangeSamplesToRender(kFrameWithExcessSamplesTrimmed,
                                      kStereoArrangement, samples)
                   .ok());
}

TEST(ArrangeSamplesToRender, InvalidMissingLabel) {
  const LabeledFrame kStereoLabeledFrame = {
      .label_to_samples = {{"L2", {0}}, {"R2", {10}}}};
  const std::vector<std::string> kMonoArrangement = {"M"};

  std::vector<std::vector<int32_t>> unused_samples;
  EXPECT_FALSE(ArrangeSamplesToRender(kStereoLabeledFrame, kMonoArrangement,
                                      unused_samples)
                   .ok());
}

TEST(LookupInputChannelOrderFromScalableLoudspeakerLayout,
     SucceedsForChannelBasedLayout) {
  EXPECT_TRUE(LookupInputChannelOrderFromScalableLoudspeakerLayout(
                  ChannelAudioLayerConfig::kLayoutMono)
                  .ok());
}

TEST(LookupInputChannelOrderFromScalableLoudspeakerLayout,
     FailsForReservedLayout) {
  EXPECT_FALSE(LookupInputChannelOrderFromScalableLoudspeakerLayout(
                   ChannelAudioLayerConfig::kLayoutReservedEnd)
                   .ok());
}

TEST(LookupOutputKeyFromPlaybackLayout, SucceedsForChannelBasedLayout) {
  EXPECT_TRUE(
      LookupOutputKeyFromPlaybackLayout(
          {.layout_type = Layout::kLayoutTypeLoudspeakersSsConvention,
           .specific_layout =
               LoudspeakersSsConventionLayout{
                   .sound_system =
                       LoudspeakersSsConventionLayout::kSoundSystemA_0_2_0}})
          .ok());
}

TEST(LookupOutputKeyFromPlaybackLayout, FailsOnBinauralBasedLayout) {
  EXPECT_FALSE(LookupOutputKeyFromPlaybackLayout(
                   {.layout_type = Layout::kLayoutTypeBinaural,
                    .specific_layout = LoudspeakersReservedBinauralLayout{}})
                   .ok());
}

TEST(LookupOutputKeyFromPlaybackLayout, FailsOnReservedLayout) {
  EXPECT_FALSE(LookupOutputKeyFromPlaybackLayout(
                   {.layout_type = Layout::kLayoutTypeReserved0,
                    .specific_layout = LoudspeakersReservedBinauralLayout{}})
                   .ok());
}

}  // namespace
}  // namespace renderer_utils
}  // namespace iamf_tools
