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

#include "gtest/gtest.h"
#include "iamf/cli/codec/opus_encoder_decoder.h"
#include "iamf/cli/codec/tests/encoder_test_base.h"
#include "iamf/cli/proto/codec_config.pb.h"
#include "iamf/obu/codec_config.h"
#include "iamf/obu/decoder_config/opus_decoder_config.h"
#include "iamf/obu/obu_header.h"

namespace iamf_tools {
namespace {

class OpusEncoderTest : public EncoderTestBase, public testing::Test {
 public:
  OpusEncoderTest() {
    opus_encoder_metadata_.set_target_bitrate_per_channel(48000);
    opus_encoder_metadata_.set_application(
        iamf_tools_cli_proto::APPLICATION_AUDIO);
    num_samples_per_frame_ = 120;
    input_sample_size_ = 16;
  }

  ~OpusEncoderTest() = default;

 protected:
  void ConstructEncoder() override {
    // Construct a Codec Config OBU. The only fields that should affect the
    // output are `num_samples_per_frame` and `decoder_config`.
    const CodecConfig temp = {.codec_id = CodecConfig::kCodecIdOpus,
                              .num_samples_per_frame = num_samples_per_frame_,
                              .audio_roll_distance = 0,
                              .decoder_config = opus_decoder_config_};

    CodecConfigObu codec_config(ObuHeader(), 0, temp);
    EXPECT_EQ(codec_config.Initialize().code(), expected_init_status_code_);

    encoder_ = std::make_unique<OpusEncoder>(opus_encoder_metadata_,
                                             codec_config, num_channels_);
  }

  OpusDecoderConfig opus_decoder_config_ = {
      .version_ = 1, .pre_skip_ = 312, .input_sample_rate_ = 48000};
  iamf_tools_cli_proto::OpusEncoderMetadata opus_encoder_metadata_ = {};
};  // namespace iamf_tools

TEST_F(OpusEncoderTest, FramesAreInOrder) {
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

TEST_F(OpusEncoderTest, EncodeAndFinalizes16BitFrameSucceeds) {
  input_sample_size_ = 16;
  Init();

  EncodeAudioFrame(std::vector<std::vector<int32_t>>(
      num_samples_per_frame_, std::vector<int32_t>(num_channels_, 42 << 16)));

  FinalizeAndValidateOrderOnly(1);
}

TEST_F(OpusEncoderTest, EncodeAndFinalizes16BitFrameSucceedsWithoutFloatApi) {
  input_sample_size_ = 16;
  opus_encoder_metadata_.set_use_float_api(false);
  Init();

  EncodeAudioFrame(std::vector<std::vector<int32_t>>(
      num_samples_per_frame_, std::vector<int32_t>(num_channels_, 42 << 16)));

  FinalizeAndValidateOrderOnly(1);
}

TEST_F(OpusEncoderTest, EncodeAndFinalizes24BitFrameSucceeds) {
  input_sample_size_ = 24;
  Init();

  EncodeAudioFrame(std::vector<std::vector<int32_t>>(
      num_samples_per_frame_, std::vector<int32_t>(num_channels_, 42 << 8)));

  FinalizeAndValidateOrderOnly(1);
}

TEST_F(OpusEncoderTest, EncodeAndFinalizes32BitFrameSucceeds) {
  input_sample_size_ = 32;
  Init();

  EncodeAudioFrame(std::vector<std::vector<int32_t>>(
      num_samples_per_frame_, std::vector<int32_t>(num_channels_, 42)));

  FinalizeAndValidateOrderOnly(1);
}

}  // namespace
}  // namespace iamf_tools
