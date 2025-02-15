/*
 * Copyright (c) 2023, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear
 * License and the Alliance for Open Media Patent License 1.0. If the BSD
 * 3-Clause Clear License was not distributed with this source code in the
 * LICENSE file, you can obtain it at
 * www.aomedia.org/license/software-license/bsd-3-c-c. If the Alliance for
 * Open Media Patent License 1.0 was not distributed with this source code
 * in the PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */
#include "iamf/cli/global_timing_module.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "iamf/cli/audio_element_with_data.h"
#include "iamf/cli/proto/parameter_block.pb.h"
#include "iamf/cli/proto/user_metadata.pb.h"
#include "iamf/cli/tests/cli_test_utils.h"
#include "iamf/obu/codec_config.h"
#include "iamf/obu/leb128.h"
#include "iamf/obu/param_definitions.h"
#include "src/google/protobuf/text_format.h"

namespace iamf_tools {
namespace {

const DecodedUleb128 kCodecConfigId = 0;
const DecodedUleb128 kSampleRate = 48000;
const DecodedUleb128 kFirstAudioElementId = 0;
const DecodedUleb128 kFirstAudioFrameId = 1000;
const DecodedUleb128 kFirstParameterId = 0;
const DecodedUleb128 kParameterIdForLoggingPurposes = kFirstParameterId;

class GlobalTimingModuleTest : public ::testing::Test {
 protected:
  void InitializeForTestingValidateParameterBlockCoverage() {
    AddLpcmCodecConfigWithIdAndSampleRate(kCodecConfigId, kSampleRate,
                                          codec_config_obus_);
    AddAmbisonicsMonoAudioElementWithSubstreamIds(
        kFirstAudioElementId, kCodecConfigId, {1000}, codec_config_obus_,
        audio_elements_);
    EXPECT_TRUE(Initialize().ok());

    TestGetNextAudioFrameStamps(kFirstAudioFrameId, 512, 0, 512);
    TestGetNextAudioFrameStamps(kFirstAudioFrameId, 512, 512, 1024);
  }

  // Constructs and initializes `global_timing_module_`.
  absl::Status Initialize() {
    global_timing_module_ = std::make_unique<GlobalTimingModule>(
        GlobalTimingModule(user_metadata_));

    // Normally the `ParamDefinitions` are stored in the descriptor OBUs. For
    // simplicity they are stored in the class. Transform it to a map of
    // pointers to pass to `Initialize`.
    absl::flat_hash_map<DecodedUleb128, const ParamDefinition*>
        parameter_id_to_param_definition_pointer = {};
    for (const auto& [parameter_id, param_definition] : param_definitions_) {
      parameter_id_to_param_definition_pointer[parameter_id] =
          param_definition.get();
    }

    return global_timing_module_->Initialize(
        audio_elements_, codec_config_obus_,
        parameter_id_to_param_definition_pointer);
  }

  void TestGetNextAudioFrameStamps(
      DecodedUleb128 substream_id, uint32_t duration,
      int32_t expected_start_timestamp, int32_t expected_end_timestamp,
      absl::StatusCode expected_status_code = absl::StatusCode::kOk) {
    int32_t start_timestamp;
    int32_t end_timestamp;
    EXPECT_EQ(global_timing_module_
                  ->GetNextAudioFrameTimestamps(substream_id, duration,
                                                start_timestamp, end_timestamp)
                  .code(),
              expected_status_code);
    EXPECT_EQ(start_timestamp, expected_start_timestamp);
    EXPECT_EQ(end_timestamp, expected_end_timestamp);
  }

  void TestGetNextParameterBlockTimestamps(DecodedUleb128 parameter_id,
                                           int32_t input_start_timestamp,
                                           uint32_t duration,
                                           int32_t expected_start_timestamp,
                                           int32_t expected_end_timestamp) {
    int32_t start_timestamp;
    int32_t end_timestamp;
    EXPECT_TRUE(global_timing_module_
                    ->GetNextParameterBlockTimestamps(
                        parameter_id, input_start_timestamp, duration,
                        start_timestamp, end_timestamp)
                    .ok());
    EXPECT_EQ(start_timestamp, expected_start_timestamp);
    EXPECT_EQ(end_timestamp, expected_end_timestamp);
  }

  iamf_tools_cli_proto::UserMetadata user_metadata_ = {};
  std::unique_ptr<GlobalTimingModule> global_timing_module_ = nullptr;

 protected:
  absl::flat_hash_map<uint32_t, CodecConfigObu> codec_config_obus_ = {};
  absl::flat_hash_map<DecodedUleb128, AudioElementWithData> audio_elements_ =
      {};
  absl::flat_hash_map<DecodedUleb128, std::unique_ptr<ParamDefinition>>
      param_definitions_ = {};
};

TEST_F(GlobalTimingModuleTest, OneSubstream) {
  AddLpcmCodecConfigWithIdAndSampleRate(kCodecConfigId, kSampleRate,
                                        codec_config_obus_);
  AddAmbisonicsMonoAudioElementWithSubstreamIds(
      kFirstAudioElementId, kCodecConfigId, {kFirstAudioFrameId},
      codec_config_obus_, audio_elements_);
  EXPECT_TRUE(Initialize().ok());

  TestGetNextAudioFrameStamps(kFirstAudioFrameId, 128, 0, 128);
  TestGetNextAudioFrameStamps(kFirstAudioFrameId, 128, 128, 256);
  TestGetNextAudioFrameStamps(kFirstAudioFrameId, 128, 256, 384);
}

TEST_F(GlobalTimingModuleTest, InvalidUnknownSubstreamId) {
  AddLpcmCodecConfigWithIdAndSampleRate(kCodecConfigId, kSampleRate,
                                        codec_config_obus_);
  AddAmbisonicsMonoAudioElementWithSubstreamIds(
      kFirstAudioElementId, kCodecConfigId, {0}, codec_config_obus_,
      audio_elements_);
  EXPECT_TRUE(Initialize().ok());

  const DecodedUleb128 kUnknownSubstreamId = 9999;
  TestGetNextAudioFrameStamps(kUnknownSubstreamId, 128, 0, 128,
                              absl::StatusCode::kInvalidArgument);
}

TEST_F(GlobalTimingModuleTest, InvalidDuplicateSubstreamIds) {
  AddLpcmCodecConfigWithIdAndSampleRate(kCodecConfigId, kSampleRate,
                                        codec_config_obus_);
  const DecodedUleb128 kDuplicateSubsteamId = kFirstAudioFrameId;
  AddAmbisonicsMonoAudioElementWithSubstreamIds(
      kFirstAudioElementId, kCodecConfigId,
      {kDuplicateSubsteamId, kDuplicateSubsteamId}, codec_config_obus_,
      audio_elements_);
  EXPECT_EQ(Initialize().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(GlobalTimingModuleTest, TwoAudioElements) {
  AddLpcmCodecConfigWithIdAndSampleRate(kCodecConfigId, kSampleRate,
                                        codec_config_obus_);
  AddAmbisonicsMonoAudioElementWithSubstreamIds(
      kFirstAudioElementId, kCodecConfigId, {kFirstAudioFrameId},
      codec_config_obus_, audio_elements_);
  const DecodedUleb128 kSecondAudioElementId = 1;
  ASSERT_NE(kFirstAudioElementId, kSecondAudioElementId);
  AddAmbisonicsMonoAudioElementWithSubstreamIds(
      kSecondAudioElementId, kCodecConfigId, {2000}, codec_config_obus_,
      audio_elements_);
  EXPECT_TRUE(Initialize().ok());

  // All subtreams have separate time keeping functionality.
  TestGetNextAudioFrameStamps(kFirstAudioFrameId, 128, 0, 128);
  TestGetNextAudioFrameStamps(kFirstAudioFrameId, 128, 128, 256);
  TestGetNextAudioFrameStamps(kFirstAudioFrameId, 128, 256, 384);

  TestGetNextAudioFrameStamps(2000, 256, 0, 256);
  TestGetNextAudioFrameStamps(2000, 256, 256, 512);
}

TEST_F(GlobalTimingModuleTest, OneParameterId) {
  AddLpcmCodecConfigWithIdAndSampleRate(kCodecConfigId, kSampleRate,
                                        codec_config_obus_);
  // The timing model does not care about the specific type of parameter. Use a
  // generic one.
  AddParamDefinitionWithMode0AndOneSubblock(kFirstParameterId,
                                            /*parameter_rate=*/kSampleRate, 64,
                                            param_definitions_);
  EXPECT_TRUE(Initialize().ok());

  TestGetNextParameterBlockTimestamps(kFirstParameterId, 0, 64, 0, 64);
  TestGetNextParameterBlockTimestamps(kFirstParameterId, 64, 64, 64, 128);
  TestGetNextParameterBlockTimestamps(kFirstParameterId, 128, 64, 128, 192);
}

TEST_F(GlobalTimingModuleTest,
       SupportsStrayParameterBlocksWithOneCodecConfigObu) {
  AddLpcmCodecConfigWithIdAndSampleRate(kCodecConfigId, kSampleRate,
                                        codec_config_obus_);

  // Stray parameters are represented by parameter blocks in the user metadata,
  // without a corresponding `ParamDefinition` in the descriptor OBUs.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        parameter_id: 0
        duration: 64
        constant_subblock_duration: 64
        num_subblocks: 1
        start_timestamp: 0
      )pb",
      user_metadata_.add_parameter_block_metadata()));

  EXPECT_TRUE(Initialize().ok());

  // Timing can be generated as expected. It has an implicit `parameter_rate`
  // matching the sampler rate of the Codec Config OBU.
  TestGetNextParameterBlockTimestamps(kFirstParameterId, 0, 64, 0, 64);
  TestGetNextParameterBlockTimestamps(kFirstParameterId, 64, 64, 64, 128);
  TestGetNextParameterBlockTimestamps(kFirstParameterId, 128, 64, 128, 192);
}

TEST_F(GlobalTimingModuleTest,
       InvalidWhenThereAreStrayParameterBlocksWithoutCodecConfigObu) {
  // Stray parameters are represented by parameter blocks in the user metadata,
  // without a corresponding `ParamDefinition` in the descriptor OBUs.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        parameter_id: 0
        duration: 64
        constant_subblock_duration: 64
        num_subblocks: 1
        start_timestamp: 0
      )pb",
      user_metadata_.add_parameter_block_metadata()));

  EXPECT_FALSE(Initialize().ok());
}

TEST_F(GlobalTimingModuleTest, InvalidWhenParameterRateIsZero) {
  AddLpcmCodecConfigWithIdAndSampleRate(kCodecConfigId, kSampleRate,
                                        codec_config_obus_);
  // The timing model does not care about the specific type of parameter. Use a
  // generic one.
  AddParamDefinitionWithMode0AndOneSubblock(kFirstParameterId,
                                            /*parameter_rate=*/0, 64,
                                            param_definitions_);
  EXPECT_FALSE(Initialize().ok());
}

TEST_F(GlobalTimingModuleTest, ValidatesParameterBlockCoverage) {
  InitializeForTestingValidateParameterBlockCoverage();

  EXPECT_TRUE(
      global_timing_module_
          ->ValidateParameterBlockCoversAudioFrame(
              kParameterIdForLoggingPurposes, 0, 1024, kFirstAudioFrameId)
          .ok());
}

TEST_F(GlobalTimingModuleTest, InvalidWhenParameterStreamEndsEarly) {
  InitializeForTestingValidateParameterBlockCoverage();

  EXPECT_FALSE(
      global_timing_module_
          ->ValidateParameterBlockCoversAudioFrame(
              kParameterIdForLoggingPurposes, 0, 1023, kFirstAudioFrameId)
          .ok());
}

TEST_F(GlobalTimingModuleTest,
       InvalidWhenParameterStreamStartsLateAndEndsSameTime) {
  InitializeForTestingValidateParameterBlockCoverage();

  EXPECT_FALSE(
      global_timing_module_
          ->ValidateParameterBlockCoversAudioFrame(
              kParameterIdForLoggingPurposes, 1, 1024, kFirstAudioFrameId)
          .ok());
}

TEST_F(GlobalTimingModuleTest,
       InvalidWhenParameterStreamStartsLateAndHasSameDuration) {
  InitializeForTestingValidateParameterBlockCoverage();

  EXPECT_FALSE(
      global_timing_module_
          ->ValidateParameterBlockCoversAudioFrame(
              kParameterIdForLoggingPurposes, 1, 1025, kFirstAudioFrameId)
          .ok());
}

}  // namespace
}  // namespace iamf_tools
