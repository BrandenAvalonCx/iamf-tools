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
#include "iamf/cli/parameters_manager.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "iamf/cli/audio_element_with_data.h"
#include "iamf/cli/parameter_block_with_data.h"
#include "iamf/cli/tests/cli_test_utils.h"
#include "iamf/obu/codec_config.h"
#include "iamf/obu/demixing_info_param_data.h"
#include "iamf/obu/leb128.h"
#include "iamf/obu/obu_header.h"
#include "iamf/obu/param_definitions.h"
#include "iamf/obu/parameter_block.h"

namespace iamf_tools {
namespace {

constexpr DecodedUleb128 kCodecConfigId = 1450;
constexpr DecodedUleb128 kSampleRate = 16000;
constexpr DecodedUleb128 kAudioElementId = 157;
constexpr DecodedUleb128 kParameterId = 995;
constexpr DecodedUleb128 kDuration = 8;
constexpr DemixingInfoParameterData::DMixPMode kDMixPMode =
    DemixingInfoParameterData::kDMixPMode3_n;

absl::Status AddOneDemixingParameterBlock(
    const ParamDefinition& param_definition, int32_t start_timestamp,
    PerIdParameterMetadata* per_id_metadata,
    std::vector<ParameterBlockWithData>& parameter_blocks) {
  *per_id_metadata = {
      .param_definition_type = ParamDefinition::kParameterDefinitionDemixing,
      .param_definition = param_definition,
  };
  parameter_blocks.emplace_back(std::make_unique<ParameterBlockObu>(
                                    ObuHeader(), kParameterId, per_id_metadata),
                                start_timestamp, start_timestamp + kDuration);
  ParameterBlockObu& parameter_block_obu = *parameter_blocks.back().obu;
  absl::Status status =
      parameter_block_obu.InitializeSubblocks(kDuration, kDuration, 1);
  status.Update(parameter_block_obu.SetSubblockDuration(0, kDuration));
  DemixingInfoParameterData demixing_info_param_data;
  demixing_info_param_data.dmixp_mode = kDMixPMode;
  parameter_block_obu.subblocks_[0].param_data = demixing_info_param_data;

  return status;
}

class ParametersManagerTest : public testing::Test {
 public:
  ParametersManagerTest() {
    AddLpcmCodecConfigWithIdAndSampleRate(kCodecConfigId, kSampleRate,
                                          codec_config_obus_);
    AddAmbisonicsMonoAudioElementWithSubstreamIds(
        kAudioElementId, kCodecConfigId,
        /*substream_ids=*/{100}, codec_config_obus_, audio_elements_);

    auto& audio_element_obu = audio_elements_.at(kAudioElementId).obu;
    AddDemixingParamDefinition(kParameterId, kSampleRate, kDuration,
                               audio_element_obu,
                               /*param_definitions=*/nullptr);

    EXPECT_TRUE(
        AddOneDemixingParameterBlock(
            *audio_element_obu.audio_element_params_[0].param_definition,
            /*start_timestamp=*/0, &per_id_metadata_, parameter_blocks_)
            .ok());
  }

 protected:
  absl::flat_hash_map<uint32_t, CodecConfigObu> codec_config_obus_;
  absl::flat_hash_map<DecodedUleb128, AudioElementWithData> audio_elements_;
  std::vector<ParameterBlockWithData> parameter_blocks_;
  PerIdParameterMetadata per_id_metadata_;
  std::unique_ptr<ParametersManager> parameters_manager_;
};

TEST_F(ParametersManagerTest, InitializeSucceeds) {
  parameters_manager_ = std::make_unique<ParametersManager>(audio_elements_);
  EXPECT_TRUE(parameters_manager_->Initialize().ok());
}

TEST_F(ParametersManagerTest, InitializeWithTwoDemixingParametersFails) {
  // Add one more demixing parameter definition, which is disallowed.
  AddDemixingParamDefinition(kParameterId, kSampleRate, kDuration,
                             audio_elements_.at(kAudioElementId).obu,
                             /*param_definitions=*/nullptr);

  parameters_manager_ = std::make_unique<ParametersManager>(audio_elements_);
  EXPECT_FALSE(parameters_manager_->Initialize().ok());
}

TEST_F(ParametersManagerTest, DemixingParamDefinitionIsAvailable) {
  parameters_manager_ = std::make_unique<ParametersManager>(audio_elements_);
  ASSERT_TRUE(parameters_manager_->Initialize().ok());

  EXPECT_TRUE(
      parameters_manager_->DemixingParamDefinitionAvailable(kAudioElementId));
}

TEST_F(ParametersManagerTest, GetDownMixingParametersSucceeds) {
  parameters_manager_ = std::make_unique<ParametersManager>(audio_elements_);
  ASSERT_TRUE(parameters_manager_->Initialize().ok());
  parameters_manager_->AddDemixingParameterBlock(&parameter_blocks_[0]);

  DownMixingParams down_mixing_params;
  EXPECT_TRUE(parameters_manager_
                  ->GetDownMixingParameters(kAudioElementId, down_mixing_params)
                  .ok());

  // Validate the values correspond to `kDMixPMode3_n`.
  EXPECT_FLOAT_EQ(down_mixing_params.alpha, 1.0);
  EXPECT_FLOAT_EQ(down_mixing_params.beta, 0.866);
  EXPECT_FLOAT_EQ(down_mixing_params.gamma, 0.866);
  EXPECT_FLOAT_EQ(down_mixing_params.delta, 0.866);
  EXPECT_EQ(down_mixing_params.w_idx_offset, 1);
  EXPECT_EQ(down_mixing_params.w_idx_used, 0);
  EXPECT_FLOAT_EQ(down_mixing_params.w, 0.0);
}

TEST_F(ParametersManagerTest, ParameterBlocksRunOutReturnsDefault) {
  parameters_manager_ = std::make_unique<ParametersManager>(audio_elements_);
  ASSERT_TRUE(parameters_manager_->Initialize().ok());
  parameters_manager_->AddDemixingParameterBlock(&parameter_blocks_[0]);

  DownMixingParams down_mixing_params;
  EXPECT_TRUE(parameters_manager_
                  ->GetDownMixingParameters(kAudioElementId, down_mixing_params)
                  .ok());

  EXPECT_TRUE(parameters_manager_
                  ->UpdateDemixingState(kAudioElementId,
                                        /*expected_timestamp=*/0)
                  .ok());

  // Get the parameters for the second time. Since there is only one
  // parameter block and is already used up the previous time, the function
  // will not find a parameter block and will return default values.
  EXPECT_TRUE(parameters_manager_
                  ->GetDownMixingParameters(kAudioElementId, down_mixing_params)
                  .ok());

  // Validate the values correspond to `kDMixPMode1` and `default_w = 10`,
  // which are the default set in `AddDemixingParamDefinition()`.
  EXPECT_FLOAT_EQ(down_mixing_params.alpha, 1.0);
  EXPECT_FLOAT_EQ(down_mixing_params.beta, 1.0);
  EXPECT_FLOAT_EQ(down_mixing_params.gamma, 0.707);
  EXPECT_FLOAT_EQ(down_mixing_params.delta, 0.707);
  EXPECT_EQ(down_mixing_params.w_idx_offset, -1);
  EXPECT_EQ(down_mixing_params.w_idx_used, 10);
  EXPECT_FLOAT_EQ(down_mixing_params.w, 0.5);

  // `UpdateDemixingState()` also succeeds, because technically there's
  // nothing to update.
  EXPECT_TRUE(parameters_manager_
                  ->UpdateDemixingState(kAudioElementId,
                                        /*expected_timestanmp=*/8)
                  .ok());
}

TEST_F(ParametersManagerTest, ParameterIdNotFoundReturnsDefault) {
  // Modify the parameter definition of the audio element so it does not
  // correspond to any parameter blocks inside `parameter_blocks_`.
  auto& audio_element = audio_elements_.at(kAudioElementId);
  audio_element.obu.audio_element_params_[0].param_definition->parameter_id_ =
      kParameterId + 1;

  // Create the parameters manager and get down mixing parameters; default
  // values are returned because the parameter ID is different from those
  // in the `parameter_blocks_`.
  parameters_manager_ = std::make_unique<ParametersManager>(audio_elements_);
  ASSERT_TRUE(parameters_manager_->Initialize().ok());
  parameters_manager_->AddDemixingParameterBlock(&parameter_blocks_[0]);

  DownMixingParams down_mixing_params;
  EXPECT_TRUE(parameters_manager_
                  ->GetDownMixingParameters(kAudioElementId, down_mixing_params)
                  .ok());

  // Validate the values correspond to `kDMixPMode1` and `default_w = 10`,
  // which are the default set in `AddDemixingParamDefinition()`.
  EXPECT_FLOAT_EQ(down_mixing_params.alpha, 1.0);
  EXPECT_FLOAT_EQ(down_mixing_params.beta, 1.0);
  EXPECT_FLOAT_EQ(down_mixing_params.gamma, 0.707);
  EXPECT_FLOAT_EQ(down_mixing_params.delta, 0.707);
  EXPECT_EQ(down_mixing_params.w_idx_offset, -1);
  EXPECT_EQ(down_mixing_params.w_idx_used, 10);
  EXPECT_FLOAT_EQ(down_mixing_params.w, 0.5);
}

TEST_F(ParametersManagerTest, GetDownMixingParametersTwiceDifferentW) {
  // Add another parameter block, so we can get down-mix parameters twice.
  ASSERT_TRUE(AddOneDemixingParameterBlock(*audio_elements_.at(kAudioElementId)
                                                .obu.audio_element_params_[0]
                                                .param_definition,
                                           /*start_timestamp=*/kDuration,
                                           &per_id_metadata_, parameter_blocks_)
                  .ok());

  parameters_manager_ = std::make_unique<ParametersManager>(audio_elements_);
  ASSERT_TRUE(parameters_manager_->Initialize().ok());
  parameters_manager_->AddDemixingParameterBlock(&parameter_blocks_[0]);

  // Get down-mix parameters for the first time.
  DownMixingParams down_mixing_params;
  ASSERT_TRUE(parameters_manager_
                  ->GetDownMixingParameters(kAudioElementId, down_mixing_params)
                  .ok());
  EXPECT_TRUE(parameters_manager_
                  ->UpdateDemixingState(kAudioElementId,
                                        /*expected_timestamp=*/0)
                  .ok());

  // The first time `w_idx` is 0, and the corresponding `w` is 0.
  const double kWFirst = 0.0;
  const double kWSecond = 0.0179;
  EXPECT_FLOAT_EQ(down_mixing_params.w, kWFirst);

  // Add and get down-mix parameters for the second time.
  parameters_manager_->AddDemixingParameterBlock(&parameter_blocks_[1]);
  EXPECT_TRUE(parameters_manager_
                  ->GetDownMixingParameters(kAudioElementId, down_mixing_params)
                  .ok());

  // Validate the values correspond to `kDMixPMode3_n`. Since `w_idx` has
  // been updated to 1, `w` becomes 0.0179.
  EXPECT_FLOAT_EQ(down_mixing_params.alpha, 1.0);
  EXPECT_FLOAT_EQ(down_mixing_params.beta, 0.866);
  EXPECT_FLOAT_EQ(down_mixing_params.gamma, 0.866);
  EXPECT_FLOAT_EQ(down_mixing_params.delta, 0.866);
  EXPECT_EQ(down_mixing_params.w_idx_offset, 1);
  EXPECT_EQ(down_mixing_params.w_idx_used, 1);

  // Updated `w`, different from the first time above.
  EXPECT_FLOAT_EQ(down_mixing_params.w, kWSecond);
}

TEST_F(ParametersManagerTest, GetDownMixingParametersTwiceWithoutUpdateSameW) {
  // Add another parameter block, so it is possible to get down-mix parameters
  // twice.
  ASSERT_TRUE(AddOneDemixingParameterBlock(*audio_elements_.at(kAudioElementId)
                                                .obu.audio_element_params_[0]
                                                .param_definition,
                                           /*start_timestamp=*/kDuration,
                                           &per_id_metadata_, parameter_blocks_)
                  .ok());

  parameters_manager_ = std::make_unique<ParametersManager>(audio_elements_);
  ASSERT_TRUE(parameters_manager_->Initialize().ok());
  parameters_manager_->AddDemixingParameterBlock(&parameter_blocks_[0]);

  // Get down-mix parameters twice without calling
  // `AddDemixingParameterBlock()` and `UpdateDemixngState()`; the same
  // down-mix parameters will be returned.
  DownMixingParams down_mixing_params;
  ASSERT_TRUE(parameters_manager_
                  ->GetDownMixingParameters(kAudioElementId, down_mixing_params)
                  .ok());

  // The first time `w_idx` is 0, and the corresponding `w` is 0.
  EXPECT_EQ(down_mixing_params.w_idx_used, 0);
  EXPECT_FLOAT_EQ(down_mixing_params.w, 0.0);

  EXPECT_TRUE(parameters_manager_
                  ->GetDownMixingParameters(kAudioElementId, down_mixing_params)
                  .ok());

  // Validate the values correspond to `kDMixPMode3_n`. Since `w_idx` has
  // NOT been updated, `w` remains 0.0.
  EXPECT_FLOAT_EQ(down_mixing_params.alpha, 1.0);
  EXPECT_FLOAT_EQ(down_mixing_params.beta, 0.866);
  EXPECT_FLOAT_EQ(down_mixing_params.gamma, 0.866);
  EXPECT_FLOAT_EQ(down_mixing_params.delta, 0.866);
  EXPECT_EQ(down_mixing_params.w_idx_offset, 1);
  EXPECT_EQ(down_mixing_params.w_idx_used, 0);
  EXPECT_FLOAT_EQ(down_mixing_params.w, 0.0);
}

TEST_F(ParametersManagerTest,
       TwoAudioElementGettingParameterBlocksWithDifferentTimestampsFails) {
  // Add another parameter block, so we can get down-mix parameters twice.
  ASSERT_TRUE(AddOneDemixingParameterBlock(*audio_elements_.at(kAudioElementId)
                                                .obu.audio_element_params_[0]
                                                .param_definition,
                                           /*start_timestamp=*/kDuration,
                                           &per_id_metadata_, parameter_blocks_)
                  .ok());

  // Add a second audio element sharing the same demixing parameter.
  constexpr DecodedUleb128 kAudioElementId2 = kAudioElementId + 1;
  AddAmbisonicsMonoAudioElementWithSubstreamIds(
      kAudioElementId2, kCodecConfigId,
      /*substream_ids=*/{200}, codec_config_obus_, audio_elements_);
  auto& second_audio_element_obu = audio_elements_.at(kAudioElementId2).obu;
  AddDemixingParamDefinition(kParameterId, kSampleRate, kDuration,
                             second_audio_element_obu,
                             /*param_definitions=*/nullptr);

  parameters_manager_ = std::make_unique<ParametersManager>(audio_elements_);
  ASSERT_TRUE(parameters_manager_->Initialize().ok());
  parameters_manager_->AddDemixingParameterBlock(&parameter_blocks_[0]);

  // Get down-mix parameters for the first audio element corresponding to the
  // first frame; the `w` value is 0.
  const double kWFirst = 0.0;
  const double kWSecond = 0.0179;
  DownMixingParams down_mixing_params;
  ASSERT_TRUE(parameters_manager_
                  ->GetDownMixingParameters(kAudioElementId, down_mixing_params)
                  .ok());
  EXPECT_TRUE(parameters_manager_
                  ->UpdateDemixingState(kAudioElementId,
                                        /*expected_timestamp=*/0)
                  .ok());
  EXPECT_FLOAT_EQ(down_mixing_params.w, kWFirst);

  // Add the parameter block for the first audio element corresponding to the
  // second frame.
  parameters_manager_->AddDemixingParameterBlock(&parameter_blocks_[1]);
  ASSERT_TRUE(parameters_manager_
                  ->GetDownMixingParameters(kAudioElementId, down_mixing_params)
                  .ok());
  EXPECT_FLOAT_EQ(down_mixing_params.w, kWSecond);

  // Get down-mix parameters for the second audio element. The second audio
  // element shares the same parameter ID, but is still expecting the
  // parameter block for the first frame (while the manager is already
  // holding the parameter block for the second frame). So the getter fails.
  EXPECT_FALSE(
      parameters_manager_
          ->GetDownMixingParameters(kAudioElementId2, down_mixing_params)
          .ok());
}

TEST_F(ParametersManagerTest, DemixingParamDefinitionIsNotAvailableForWrongId) {
  parameters_manager_ = std::make_unique<ParametersManager>(audio_elements_);
  ASSERT_TRUE(parameters_manager_->Initialize().ok());
  parameters_manager_->AddDemixingParameterBlock(&parameter_blocks_[0]);

  const DecodedUleb128 kWrongAudioElementId = kAudioElementId + 1;
  EXPECT_FALSE(parameters_manager_->DemixingParamDefinitionAvailable(
      kWrongAudioElementId));

  // However, `GetDownMixingParameters()` still succeeds.
  DownMixingParams down_mixing_params;
  EXPECT_TRUE(
      parameters_manager_
          ->GetDownMixingParameters(kWrongAudioElementId, down_mixing_params)
          .ok());

  // `UpdateDemixingState()` also succeeds.
  EXPECT_TRUE(
      parameters_manager_->UpdateDemixingState(kWrongAudioElementId, 0).ok());
}

TEST_F(ParametersManagerTest, UpdateFailsWithWrongTimestamps) {
  parameters_manager_ = std::make_unique<ParametersManager>(audio_elements_);
  ASSERT_TRUE(parameters_manager_->Initialize().ok());
  parameters_manager_->AddDemixingParameterBlock(&parameter_blocks_[0]);

  // The first frame starts with timestamp = 0, so updating with a different
  // timestamp fails.
  const int32_t kWrongTimestamp = 8;
  EXPECT_FALSE(
      parameters_manager_->UpdateDemixingState(kAudioElementId, kWrongTimestamp)
          .ok());
}

TEST_F(ParametersManagerTest, UpdateNotValidatingWhenParameterIdNotFound) {
  // Modify the parameter definition of the audio element so it does not
  // correspond to any parameter blocks inside `parameter_blocks_`.
  auto& audio_element = audio_elements_.at(kAudioElementId);
  audio_element.obu.audio_element_params_[0].param_definition->parameter_id_ =
      kParameterId + 1;

  // Create the parameters manager and get down mixing parameters; default
  // values are returned because the parameter ID is not found.
  parameters_manager_ = std::make_unique<ParametersManager>(audio_elements_);
  ASSERT_TRUE(parameters_manager_->Initialize().ok());
  parameters_manager_->AddDemixingParameterBlock(&parameter_blocks_[0]);

  // `UpdateDemixingState()` succeeds with any timestamp passed in,
  // because no validation is performed.
  for (const int32_t timestamp : {0, 8, -200, 61, 4772}) {
    EXPECT_TRUE(
        parameters_manager_->UpdateDemixingState(kAudioElementId, timestamp)
            .ok());
  }
}

}  // namespace
}  // namespace iamf_tools
