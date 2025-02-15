/*
 * Copyright (c) 2023, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at www.aomedia.org/license/software-license/bsd-3-c-c. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * www.aomedia.org/license/patent.
 */
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "iamf/cli/codec/aac_encoder_decoder.h"
#include "iamf/cli/codec/tests/encoder_test_base.h"
#include "iamf/cli/proto/codec_config.pb.h"
#include "iamf/obu/codec_config.h"
#include "iamf/obu/decoder_config/aac_decoder_config.h"
#include "iamf/obu/obu_header.h"

namespace iamf_tools {
namespace {

class AacEncoderTest : public EncoderTestBase, public testing::Test {
 public:
  AacEncoderTest() {
    aac_encoder_metadata_.set_bitrate_mode(0);
    aac_encoder_metadata_.set_enable_afterburner(true);
    aac_encoder_metadata_.set_signaling_mode(2);
    num_samples_per_frame_ = 1024;
    input_sample_size_ = 16;
  }

  ~AacEncoderTest() = default;

 protected:
  void ConstructEncoder() override {
    // Construct a Codec Config OBU. The only fields that should affect the
    // output are `num_samples_per_frame` and `decoder_config`.
    const CodecConfig temp = {.codec_id = CodecConfig::kCodecIdAacLc,
                              .num_samples_per_frame = num_samples_per_frame_,
                              .audio_roll_distance = 0,
                              .decoder_config = aac_decoder_config_};

    CodecConfigObu codec_config(ObuHeader(), 0, temp);
    ASSERT_TRUE(codec_config.Initialize().ok());

    encoder_ = std::make_unique<AacEncoder>(aac_encoder_metadata_, codec_config,
                                            num_channels_);
  }

  AacDecoderConfig aac_decoder_config_ = {
      .reserved_ = 0,
      .buffer_size_db_ = 0,
      .max_bitrate_ = 0,
      .average_bit_rate_ = 0,
      .decoder_specific_info_ =
          {.audio_specific_config =
               {.sample_frequency_index_ =
                    AudioSpecificConfig::kSampleFrequencyIndex64000}},
  };
  iamf_tools_cli_proto::AacEncoderMetadata aac_encoder_metadata_ = {};
};  // namespace iamf_tools

TEST_F(AacEncoderTest, FramesAreInOrder) {
  Init();

  // Encode several frames and ensure the correct number of frames are output in
  // the same order as the input.
  const int kNumFrames = 100;
  for (int i = 0; i < kNumFrames; i++) {
    EncodeAudioFrame(std::vector<std::vector<int32_t>>(
        num_samples_per_frame_, std::vector<int32_t>(num_channels_, i)));
  }
  FinalizeAndValidateOrderOnly(kNumFrames);
}

TEST_F(AacEncoderTest, InitSucceedsWithDefaultAacEncoderMetadata) {
  aac_encoder_metadata_ = {};
  Init();
}

TEST_F(AacEncoderTest, InitSucceedsWithAfterburnerEnabled) {
  aac_encoder_metadata_.set_enable_afterburner(true);
  Init();
}

TEST_F(AacEncoderTest, InitSucceedsWithAfterburnerDisabled) {
  aac_encoder_metadata_.set_enable_afterburner(false);
  Init();
}

TEST_F(AacEncoderTest, InitFailsWithInvalidBitrateMode) {
  aac_encoder_metadata_.set_bitrate_mode(-1);
  expected_init_status_code_ = absl::StatusCode::kFailedPrecondition;
  Init();
}

TEST_F(AacEncoderTest, InitFailsWithInvalidSignalingMode) {
  aac_encoder_metadata_.set_signaling_mode(-1);
  expected_init_status_code_ = absl::StatusCode::kFailedPrecondition;
  Init();
}

}  // namespace
}  // namespace iamf_tools
