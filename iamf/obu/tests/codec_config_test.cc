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
#include "iamf/obu/codec_config.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "iamf/cli/leb_generator.h"
#include "iamf/common/read_bit_buffer.h"
#include "iamf/common/write_bit_buffer.h"
#include "iamf/obu/decoder_config/lpcm_decoder_config.h"
#include "iamf/obu/decoder_config/opus_decoder_config.h"
#include "iamf/obu/leb128.h"
#include "iamf/obu/obu_header.h"
#include "iamf/obu/tests/obu_test_base.h"

namespace iamf_tools {
namespace {

class CodecConfigTestBase : public ObuTestBase {
 public:
  CodecConfigTestBase(CodecConfig::CodecId codec_id,
                      DecoderConfig decoder_config)
      : ObuTestBase(
            /*expected_header=*/{0, 14}, /*expected_payload=*/{}),
        codec_config_id_(0),
        codec_config_({.codec_id = codec_id,
                       .num_samples_per_frame = 64,
                       .audio_roll_distance = 0,
                       .decoder_config = decoder_config}) {}

  ~CodecConfigTestBase() override = default;

 protected:
  void InitExpectOk() override {
    obu_ = std::make_unique<CodecConfigObu>(header_, codec_config_id_,
                                            codec_config_);
    EXPECT_TRUE(obu_->Initialize().ok());
  }

  void WriteObuExpectOk(WriteBitBuffer& wb) override {
    EXPECT_TRUE(obu_->ValidateAndWriteObu(wb).ok());
  }

  void TestInputSampleRate() {
    EXPECT_EQ(obu_->GetInputSampleRate(), expected_input_sample_rate_);
  }

  void TestOutputSampleRate() {
    EXPECT_EQ(obu_->GetOutputSampleRate(), expected_output_sample_rate_);
  }

  void TestGetBitDepthToMeasureLoudness() {
    EXPECT_EQ(obu_->GetBitDepthToMeasureLoudness(),
              expected_output_pcm_bit_depth_);
  }

  uint32_t expected_input_sample_rate_;
  uint32_t expected_output_sample_rate_;
  uint8_t expected_output_pcm_bit_depth_;

  std::unique_ptr<CodecConfigObu> obu_;

  DecodedUleb128 codec_config_id_;
  CodecConfig codec_config_;
};

struct SampleRateTestCase {
  uint32_t sample_rate;
  bool expect_ok;
};

class CodecConfigLpcmTestForSampleRate
    : public CodecConfigTestBase,
      public testing::TestWithParam<SampleRateTestCase> {
 public:
  CodecConfigLpcmTestForSampleRate()
      : CodecConfigTestBase(
            CodecConfig::kCodecIdLpcm,
            LpcmDecoderConfig{
                .sample_format_flags_ = LpcmDecoderConfig::kLpcmBigEndian,
                .sample_size_ = 16,
                .sample_rate_ = 48000}) {}
};

// Instantiate an LPCM `CodecConfigOBU` with the specified parameters. Verify
// the validation function returns the expected status.
TEST_P(CodecConfigLpcmTestForSampleRate, TestCodecConfigLpcm) {
  // Replace the default sample rate and expected status codes.
  std::get<LpcmDecoderConfig>(codec_config_.decoder_config).sample_rate_ =
      GetParam().sample_rate;

  obu_ = std::make_unique<CodecConfigObu>(header_, codec_config_id_,
                                          codec_config_);

  const bool expect_ok = GetParam().expect_ok;
  EXPECT_EQ(obu_->Initialize().ok(), expect_ok);
  WriteBitBuffer unused_wb(0);
  EXPECT_EQ(obu_->ValidateAndWriteObu(unused_wb).ok(), expect_ok);

  if (expect_ok) {
    // Validate the functions to get the sample rate return the expected value.
    expected_output_sample_rate_ = GetParam().sample_rate;
    TestOutputSampleRate();

    // The input sample rate function for LPCM should the output sample rate
    // function.
    expected_input_sample_rate_ = expected_output_sample_rate_;
    TestInputSampleRate();
  }
}

INSTANTIATE_TEST_SUITE_P(LegalSampleRates, CodecConfigLpcmTestForSampleRate,
                         testing::ValuesIn<SampleRateTestCase>({{48000, true},
                                                                {16000, true},
                                                                {32000, true},
                                                                {44100, true},
                                                                {48000, true},
                                                                {96000,
                                                                 true}}));

INSTANTIATE_TEST_SUITE_P(
    IllegalSampleRates, CodecConfigLpcmTestForSampleRate,
    ::testing::ValuesIn<SampleRateTestCase>({{0, false},
                                             {8000, false},
                                             {22050, false},
                                             {23000, false},
                                             {196000, false}}));

class CodecConfigLpcmTest : public CodecConfigTestBase, public testing::Test {
 public:
  CodecConfigLpcmTest()
      : CodecConfigTestBase(
            CodecConfig::kCodecIdLpcm,
            LpcmDecoderConfig{
                .sample_format_flags_ = LpcmDecoderConfig::kLpcmBigEndian,
                .sample_size_ = 16,
                .sample_rate_ = 48000}) {
    expected_payload_ = {// `codec_config_id`.
                         0,
                         // `codec_id`.
                         'i', 'p', 'c', 'm',
                         // `num_samples_per_frame`.
                         64,
                         // `audio_roll_distance`.
                         0, 0,
                         // `sample_format_flags`.
                         0,
                         // `sample_size`.
                         16,
                         // `sample_rate`.
                         0, 0, 0xbb, 0x80};
  }
};

TEST_F(CodecConfigLpcmTest, ConstructorSetsObuTyoe) {
  InitExpectOk();

  EXPECT_EQ(obu_->header_.obu_type, kObuIaCodecConfig);
}

TEST_F(CodecConfigLpcmTest, NonMinimalLebGeneratorAffectsAllLeb128s) {
  leb_generator_ =
      LebGenerator::Create(LebGenerator::GenerationMode::kFixedSize, 2);
  codec_config_id_ = 0;
  codec_config_.num_samples_per_frame = 1;

  expected_header_ = {0, 0x80 | 16, 0};
  expected_payload_ = {// `codec_config_id`.
                       0x80, 0x00,
                       // `codec_id`.
                       'i', 'p', 'c', 'm',
                       // `num_samples_per_frame`.
                       0x81, 0x00,
                       // `audio_roll_distance`.
                       0, 0,
                       // `sample_format_flags`.
                       0,
                       // `sample_size`.
                       16,
                       // `sample_rate`.
                       0, 0, 0xbb, 0x80};

  InitAndTestWrite();
}

TEST_F(CodecConfigLpcmTest, InitFailsWithIllegalCodecId) {
  codec_config_.codec_id = static_cast<CodecConfig::CodecId>(0);

  obu_ = std::make_unique<CodecConfigObu>(header_, codec_config_id_,
                                          codec_config_);
  EXPECT_FALSE(obu_->Initialize().ok());
}

TEST_F(CodecConfigLpcmTest, InitializeFailsWithWriteIllegalSampleSize) {
  std::get<LpcmDecoderConfig>(codec_config_.decoder_config).sample_size_ = 33;

  obu_ = std::make_unique<CodecConfigObu>(header_, codec_config_id_,
                                          codec_config_);
  EXPECT_FALSE(obu_->Initialize().ok());
}

TEST_F(CodecConfigLpcmTest, InitializeFailsWithGetIllegalSampleSize) {
  std::get<LpcmDecoderConfig>(codec_config_.decoder_config).sample_size_ = 33;

  obu_ = std::make_unique<CodecConfigObu>(header_, codec_config_id_,
                                          codec_config_);
  EXPECT_FALSE(obu_->Initialize().ok());
}

TEST_F(CodecConfigLpcmTest,
       ValidateAndWriteFailsWithIllegalNumSamplesPerFrame) {
  codec_config_.num_samples_per_frame = 0;

  InitExpectOk();
  WriteBitBuffer unused_wb(0);
  EXPECT_FALSE(obu_->ValidateAndWriteObu(unused_wb).ok());
}

TEST_F(CodecConfigLpcmTest, Default) { InitAndTestWrite(); }

TEST_F(CodecConfigLpcmTest, ExtensionHeader) {
  header_.obu_extension_flag = true;
  header_.extension_header_size = 5;
  header_.extension_header_bytes = {'e', 'x', 't', 'r', 'a'};

  expected_header_ = {kObuIaCodecConfig << 3 | kObuExtensionFlagBitMask,
                      // `obu_size`.
                      20,
                      // `extension_header_size`.
                      5,
                      // `extension_header_bytes`.
                      'e', 'x', 't', 'r', 'a'};
  InitAndTestWrite();
}

TEST_F(CodecConfigLpcmTest, ConfigId) {
  codec_config_id_ = 100;
  expected_payload_ = {// `codec_config_id`.
                       100,
                       // `codec_id`.
                       'i', 'p', 'c', 'm',
                       // `num_samples_per_frame`.
                       64,
                       // `audio_roll_distance`.
                       0, 0,
                       // `sample_format_flags`.
                       0,
                       // `sample_size`.
                       16,
                       // `sample_rate`.
                       0, 0, 0xbb, 0x80};
  InitAndTestWrite();
}

TEST_F(CodecConfigLpcmTest, NumSamplesPerFrame) {
  codec_config_.num_samples_per_frame = 128;
  expected_header_ = {0, 15};
  expected_payload_ = {// `codec_config_id`.
                       0,
                       // `codec_id`.
                       'i', 'p', 'c', 'm',
                       // `num_samples_per_frame`.
                       0x80, 0x01,
                       // `audio_roll_distance`.
                       0, 0,
                       // `sample_format_flags`.
                       0,
                       // `sample_size`.
                       16,
                       // `sample_rate`.
                       0, 0, 0xbb, 0x80};

  InitAndTestWrite();
}

TEST_F(CodecConfigLpcmTest, SampleFormatFlags) {
  std::get<LpcmDecoderConfig>(codec_config_.decoder_config)
      .sample_format_flags_ = LpcmDecoderConfig::kLpcmLittleEndian;
  expected_payload_ = {// `codec_config_id`.
                       0,
                       // `codec_id`.
                       'i', 'p', 'c', 'm',
                       // `num_samples_per_frame`.
                       64,
                       // `audio_roll_distance`.
                       0, 0,
                       // `sample_format_flags`.
                       1,
                       // `sample_size`.
                       16,
                       // `sample_rate`.
                       0, 0, 0xbb, 0x80};

  InitAndTestWrite();
}

TEST_F(CodecConfigLpcmTest, WriteSampleSize) {
  std::get<LpcmDecoderConfig>(codec_config_.decoder_config).sample_size_ = 24;
  expected_payload_ = {// `codec_config_id`.
                       0,
                       // `codec_id`.
                       'i', 'p', 'c', 'm',
                       // `num_samples_per_frame`.
                       64,
                       // `audio_roll_distance`.
                       0, 0,
                       // `sample_format_flags`.
                       0,
                       // `sample_size`.
                       24,
                       // `sample_rate`.
                       0, 0, 0xbb, 0x80};

  InitAndTestWrite();
}

TEST_F(CodecConfigLpcmTest, GetSampleSize) {
  std::get<LpcmDecoderConfig>(codec_config_.decoder_config).sample_size_ = 24;
  expected_output_pcm_bit_depth_ = 24;
  InitExpectOk();
  TestGetBitDepthToMeasureLoudness();
}

TEST_F(CodecConfigLpcmTest, WriteSampleRate) {
  std::get<LpcmDecoderConfig>(codec_config_.decoder_config).sample_rate_ =
      16000;
  expected_payload_ = {// `codec_config_id`.
                       0,
                       // `codec_id`.
                       'i', 'p', 'c', 'm',
                       // `num_samples_per_frame`.
                       64,
                       // `audio_roll_distance`.
                       0, 0,
                       // `sample_format_flags`.
                       0,
                       // `sample_size`.
                       16,
                       // `sample_rate`.
                       0, 0, 0x3e, 0x80};

  InitAndTestWrite();
}

TEST_F(CodecConfigLpcmTest, GetOutputSampleRate) {
  std::get<LpcmDecoderConfig>(codec_config_.decoder_config).sample_rate_ =
      16000;
  expected_output_sample_rate_ = 16000;
  InitExpectOk();
  TestOutputSampleRate();
}

TEST_F(CodecConfigLpcmTest, GetInputSampleRate) {
  std::get<LpcmDecoderConfig>(codec_config_.decoder_config).sample_rate_ =
      16000;
  expected_input_sample_rate_ = 16000;
  InitExpectOk();
  TestInputSampleRate();
}

TEST_F(CodecConfigLpcmTest, RedundantCopy) {
  header_.obu_redundant_copy = true;

  expected_header_ = {kObuIaCodecConfig << 3 | kObuRedundantCopyBitMask, 14};
  InitAndTestWrite();
}

class CodecConfigOpusTest : public CodecConfigTestBase, public testing::Test {
 public:
  CodecConfigOpusTest()
      : CodecConfigTestBase(
            CodecConfig::kCodecIdOpus,
            OpusDecoderConfig{
                .version_ = 1, .pre_skip_ = 0, .input_sample_rate_ = 0}) {
    // Overwrite some default values to be more reasonable for Opus.
    codec_config_.num_samples_per_frame = 960;
    codec_config_.audio_roll_distance = -4;
    expected_header_ = {0, 20};
    expected_payload_ = {0, 'O', 'p', 'u', 's', 0xc0, 0x07, 0xff, 0xfc,
                         // Start `DecoderConfig`.
                         1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  }
};

TEST_F(CodecConfigOpusTest, ManyLargeValues) {
  leb_generator_ =
      LebGenerator::Create(LebGenerator::GenerationMode::kFixedSize, 8);
  codec_config_id_ = std::numeric_limits<DecodedUleb128>::max();
  codec_config_.num_samples_per_frame =
      std::numeric_limits<DecodedUleb128>::max();
  codec_config_.audio_roll_distance = -1;
  std::get<OpusDecoderConfig>(codec_config_.decoder_config).pre_skip_ = 0xffff;
  std::get<OpusDecoderConfig>(codec_config_.decoder_config).input_sample_rate_ =
      0xffffffff;

  expected_header_ = {0, 0x80 | 33, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00};
  expected_payload_ = {// `codec_config_id`.
                       0xff, 0xff, 0xff, 0xff, 0x8f, 0x80, 0x80, 0x00,
                       // `codec_id`.
                       'O', 'p', 'u', 's',
                       // `num_samples_per_frame`.
                       0xff, 0xff, 0xff, 0xff, 0x8f, 0x80, 0x80, 0x00,
                       // `audio_roll_distance`.
                       0xff, 0xff,
                       // Start `DecoderConfig`.
                       // `version`.
                       1,
                       // `output_channel_count`.
                       OpusDecoderConfig::kOutputChannelCount,
                       // `pre_skip`
                       0xff, 0xff,
                       //
                       // `input_sample_rate`.
                       0xff, 0xff, 0xff, 0xff,
                       // `output_gain`.
                       0, 0,
                       // `mapping_family`.
                       OpusDecoderConfig::kMappingFamily};

  InitAndTestWrite();
}

TEST_F(CodecConfigOpusTest, InitializeFailsWithIllegalCodecId) {
  codec_config_.codec_id = static_cast<CodecConfig::CodecId>(0);

  obu_ = std::make_unique<CodecConfigObu>(header_, codec_config_id_,
                                          codec_config_);
  EXPECT_FALSE(obu_->Initialize().ok());
}

TEST_F(CodecConfigOpusTest,
       ValidateAndWriteFailsWithIllegalNumSamplesPerFrame) {
  codec_config_.num_samples_per_frame = 0;

  InitExpectOk();
  WriteBitBuffer unused_wb(0);
  EXPECT_FALSE(obu_->ValidateAndWriteObu(unused_wb).ok());
}

TEST_F(CodecConfigOpusTest, Default) { InitAndTestWrite(); }

TEST_F(CodecConfigOpusTest, VarySeveralFields) {
  codec_config_id_ = 123;
  std::get<OpusDecoderConfig>(codec_config_.decoder_config).version_ = 15;
  std::get<OpusDecoderConfig>(codec_config_.decoder_config).pre_skip_ = 3;
  std::get<OpusDecoderConfig>(codec_config_.decoder_config).input_sample_rate_ =
      4;
  expected_payload_ = {123, 'O', 'p', 'u', 's', 0xc0, 0x07, 0xff, 0xfc,
                       // Start `DecoderConfig`.
                       // `version`.
                       15,
                       // `output_channel_count`.
                       OpusDecoderConfig::kOutputChannelCount,
                       // `pre_skip`
                       0, 3,
                       //
                       // `input_sample_rate`.
                       0, 0, 0, 4,
                       // `output_gain`.
                       0, 0,
                       // `mapping_family`.
                       OpusDecoderConfig::kMappingFamily};
  InitAndTestWrite();
}

TEST_F(CodecConfigOpusTest, RedundantCopy) {
  header_.obu_redundant_copy = true;
  expected_header_ = {4, 20};
  InitAndTestWrite();
}

TEST(CreateFromBuffer, OpusDecoderConfig) {
  std::vector<uint8_t> source_data = {123, 'O', 'p', 'u', 's',
                                      // num_samples_per_frame
                                      0xc0, 0x07,
                                      // audio_roll_distance
                                      0xff, 0xfc,
                                      // Start `DecoderConfig`.
                                      // `version`.
                                      15,
                                      // `output_channel_count`.
                                      OpusDecoderConfig::kOutputChannelCount,
                                      // `pre_skip`
                                      0, 3,
                                      //
                                      // `input_sample_rate`.
                                      0, 0, 0, 4,
                                      // `output_gain`.
                                      0, 0,
                                      // `mapping_family`.
                                      OpusDecoderConfig::kMappingFamily};
  ReadBitBuffer buffer(1024, &source_data);
  ObuHeader header;
  absl::StatusOr<CodecConfigObu> obu =
      CodecConfigObu::CreateFromBuffer(header, buffer);
  EXPECT_TRUE(obu.ok());

  // Set up expected data
  DecodedUleb128 expected_codec_config_id = 123;
  CodecConfig expected_codec_config = {
      .codec_id = static_cast<CodecConfig::CodecId>(CodecConfig::kCodecIdOpus),
      .num_samples_per_frame = 960,
      .audio_roll_distance = -4,
      .decoder_config =
          OpusDecoderConfig{
              .version_ = 15,
              .output_channel_count_ = OpusDecoderConfig::kOutputChannelCount,
              .pre_skip_ = 3,
              .input_sample_rate_ = 4,
              .output_gain_ = 0,
              .mapping_family_ = OpusDecoderConfig::kMappingFamily},
  };

  // Validate fields
  EXPECT_EQ(obu.value().GetCodecConfigId(), expected_codec_config_id);
  EXPECT_EQ(obu.value().GetCodecConfig(), expected_codec_config);
}

// TODO(b/331831247, b/331833384, b/331831926): Add test cases for other
// decoder configs.
TEST(CreateFromBuffer, LpcmDecoderConfigNotSupported) {
  std::vector<uint8_t> source_data = {123,  // `codec_id`.
                                      'i', 'p', 'c', 'm',
                                      // num_samples_per_frame
                                      0xc0, 0x07,
                                      // audio_roll_distance
                                      0xff, 0xfc,
                                      // Start `DecoderConfig`.
                                      // `version`.
                                      15,
                                      // `output_channel_count`.
                                      OpusDecoderConfig::kOutputChannelCount,
                                      // `pre_skip`
                                      0, 3,
                                      //
                                      // `input_sample_rate`.
                                      0, 0, 0, 4,
                                      // `output_gain`.
                                      0, 0,
                                      // `mapping_family`.
                                      OpusDecoderConfig::kMappingFamily};
  ReadBitBuffer buffer(1024, &source_data);
  ObuHeader header;
  absl::StatusOr<CodecConfigObu> obu =
      CodecConfigObu::CreateFromBuffer(header, buffer);
  EXPECT_FALSE(obu.ok());
}

}  // namespace
}  // namespace iamf_tools
