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
#include "iamf/cli/wav_sample_provider.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Placeholder for get runfiles header.
#include "absl/container/flat_hash_map.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "iamf/cli/audio_element_with_data.h"
#include "iamf/cli/demixing_module.h"
#include "iamf/cli/proto/user_metadata.pb.h"
#include "iamf/cli/tests/cli_test_utils.h"
#include "iamf/obu/codec_config.h"
#include "iamf/obu/leb128.h"
#include "src/google/protobuf/text_format.h"

namespace iamf_tools {
namespace {

static constexpr DecodedUleb128 kAudioElementId = 300;
static constexpr DecodedUleb128 kCodecConfigId = 200;
static constexpr uint32_t kSampleRate = 48000;

void InitializeTestData(
    const uint32_t sample_rate,
    iamf_tools_cli_proto::UserMetadata& user_metadata,
    absl::flat_hash_map<DecodedUleb128, AudioElementWithData>& audio_elements) {
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        wav_filename: "stereo_8_samples_48khz_s16le.wav"
        samples_to_trim_at_end: 0
        samples_to_trim_at_start: 0
        audio_element_id: 300
        channel_ids: [ 0, 1 ]
        channel_labels: [ "L2", "R2" ]
      )pb",
      user_metadata.add_audio_frame_metadata()));

  static absl::flat_hash_map<uint32_t, CodecConfigObu> codec_config_obus;
  codec_config_obus.clear();
  AddLpcmCodecConfigWithIdAndSampleRate(kCodecConfigId, sample_rate,
                                        codec_config_obus);
  const std::vector<DecodedUleb128> kSubstreamIds = {0};
  AddScalableAudioElementWithSubstreamIds(kAudioElementId, kCodecConfigId,
                                          kSubstreamIds, codec_config_obus,
                                          audio_elements);
}

std::filesystem::path GetInputWavDir() {
  static const auto input_wav_dir =
      std::filesystem::current_path() / std::string("iamf/cli/testdata");
  return input_wav_dir;
}

TEST(WavSampleProviderTest, InitializeSucceeds) {
  iamf_tools_cli_proto::UserMetadata user_metadata;
  absl::flat_hash_map<DecodedUleb128, AudioElementWithData> audio_elements;
  InitializeTestData(kSampleRate, user_metadata, audio_elements);

  WavSampleProvider wav_sample_provider(user_metadata.audio_frame_metadata());
  EXPECT_TRUE(
      wav_sample_provider.Initialize(GetInputWavDir(), audio_elements).ok());
}

TEST(WavSampleProviderTest, MismatchingChannelIdsAndLabels) {
  iamf_tools_cli_proto::UserMetadata user_metadata;
  absl::flat_hash_map<DecodedUleb128, AudioElementWithData> audio_elements;
  InitializeTestData(kSampleRate, user_metadata, audio_elements);

  // Add one extra channel label, which does not have a corresponding
  // channel ID, causing the `Initialize()` to fail.
  user_metadata.mutable_audio_frame_metadata(0)->add_channel_labels("C");

  WavSampleProvider wav_sample_provider(user_metadata.audio_frame_metadata());
  EXPECT_FALSE(
      wav_sample_provider.Initialize(GetInputWavDir(), audio_elements).ok());
}

TEST(WavSampleProviderTest, BitDepthLowerThanFile) {
  iamf_tools_cli_proto::UserMetadata user_metadata;
  absl::flat_hash_map<DecodedUleb128, AudioElementWithData> audio_elements;
  InitializeTestData(kSampleRate, user_metadata, audio_elements);

  // Try to load a 24-bit WAV file with a codec config whose bit depth is 16.
  // The `Initialize()` would refuse to lower the bit depth and fail.
  user_metadata.mutable_audio_frame_metadata(0)->set_wav_filename(
      "stereo_8_samples_48khz_s24le.wav");

  WavSampleProvider wav_sample_provider(user_metadata.audio_frame_metadata());
  EXPECT_FALSE(
      wav_sample_provider.Initialize(GetInputWavDir(), audio_elements).ok());
}

TEST(WavSampleProviderTest, MismatchingSampleRates) {
  iamf_tools_cli_proto::UserMetadata user_metadata;
  absl::flat_hash_map<DecodedUleb128, AudioElementWithData> audio_elements;

  // Set the sample rate of the codec config to a different one than the WAV
  // file, causing the `Initialize()` to fail.
  const uint32_t kWrongSampleRate = 16000;
  InitializeTestData(kWrongSampleRate, user_metadata, audio_elements);

  WavSampleProvider wav_sample_provider(user_metadata.audio_frame_metadata());
  EXPECT_FALSE(
      wav_sample_provider.Initialize(GetInputWavDir(), audio_elements).ok());
}

TEST(WavSampleProviderTest, ReadFrameSucceeds) {
  iamf_tools_cli_proto::UserMetadata user_metadata;
  absl::flat_hash_map<DecodedUleb128, AudioElementWithData> audio_elements;
  InitializeTestData(kSampleRate, user_metadata, audio_elements);

  WavSampleProvider wav_sample_provider(user_metadata.audio_frame_metadata());
  ASSERT_TRUE(
      wav_sample_provider.Initialize(GetInputWavDir(), audio_elements).ok());

  LabelSamplesMap labeled_samples;
  EXPECT_TRUE(
      wav_sample_provider.ReadFrames(kAudioElementId, labeled_samples).ok());

  // Validate samples read from the WAV file.
  const std::vector<int32_t> expected_samples_l2 = {
      1 << 16, 2 << 16, 3 << 16, 4 << 16, 5 << 16, 6 << 16, 7 << 16, 8 << 16};
  const std::vector<int32_t> expected_samples_r2 = {
      65535 << 16, 65534 << 16, 65533 << 16, 65532 << 16,
      65531 << 16, 65530 << 16, 65529 << 16, 65528 << 16};
  EXPECT_THAT(labeled_samples["L2"], testing::Eq(expected_samples_l2));
  EXPECT_THAT(labeled_samples["R2"], testing::Eq(expected_samples_r2));
}

TEST(WavSampleProviderTest, ReadFrameFailsWithWrongAudioElementId) {
  iamf_tools_cli_proto::UserMetadata user_metadata;
  absl::flat_hash_map<DecodedUleb128, AudioElementWithData> audio_elements;
  InitializeTestData(kSampleRate, user_metadata, audio_elements);

  WavSampleProvider wav_sample_provider(user_metadata.audio_frame_metadata());
  ASSERT_TRUE(
      wav_sample_provider.Initialize(GetInputWavDir(), audio_elements).ok());

  // Try to read frames using a wrong Audio Element ID.
  const auto kWrongAudioElementId = kAudioElementId + 99;
  LabelSamplesMap labeled_samples;
  EXPECT_FALSE(
      wav_sample_provider.ReadFrames(kWrongAudioElementId, labeled_samples)
          .ok());
}

TEST(WavSampleProviderTest, ReadFrameFailsWithoutCallingInitialize) {
  iamf_tools_cli_proto::UserMetadata user_metadata;
  absl::flat_hash_map<DecodedUleb128, AudioElementWithData> audio_elements;
  InitializeTestData(kSampleRate, user_metadata, audio_elements);

  WavSampleProvider wav_sample_provider(user_metadata.audio_frame_metadata());

  // Miss the following call to `Initialize()`:
  // ASSERT_TRUE(
  //     wav_sample_provider.Initialize(GetInputWavDir(), audio_elements).ok());

  LabelSamplesMap labeled_samples;
  EXPECT_FALSE(
      wav_sample_provider.ReadFrames(kAudioElementId, labeled_samples).ok());
}

}  // namespace
}  // namespace iamf_tools
