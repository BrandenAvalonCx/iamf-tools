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
#include "iamf/obu/audio_element.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "iamf/cli/leb_generator.h"
#include "iamf/common/read_bit_buffer.h"
#include "iamf/common/write_bit_buffer.h"
#include "iamf/obu/demixing_info_param_data.h"
#include "iamf/obu/leb128.h"
#include "iamf/obu/obu_header.h"
#include "iamf/obu/param_definitions.h"
#include "iamf/obu/tests/obu_test_base.h"

namespace iamf_tools {
namespace {

// TODO(b/272003291): Add more "expected failure" tests. Add more "successful"
//                    test cases to existing tests.

constexpr uint8_t kParameterDefinitionDemixingAsUint8 =
    static_cast<uint8_t>(ParamDefinition::kParameterDefinitionDemixing);

class AudioElementObuTestBase : public ObuTestBase {
 public:
  struct RequiredAudioElementArgs {
    DecodedUleb128 audio_element_id;
    AudioElementObu::AudioElementType audio_element_type;
    uint8_t reserved;

    DecodedUleb128 codec_config_id;

    DecodedUleb128 num_substreams;

    // Length `num_substreams`.
    std::vector<DecodedUleb128> substream_ids;

    DecodedUleb128 num_parameters;

    // Length `num_parameters`.
    std::vector<AudioElementParam> audio_element_params;
  };

  AudioElementObuTestBase(AudioElementObu::AudioElementType audio_element_type)
      : ObuTestBase(
            /*expected_header=*/{kObuIaAudioElement << 3, 21},
            /*expected_payload=*/{}),
        obu_(nullptr),
        required_args_({
            .audio_element_id = 1,
            .audio_element_type = audio_element_type,
            .reserved = 0,
            .codec_config_id = 2,
            .num_substreams = 1,
            .substream_ids = {3},
            .num_parameters = 1,
            .audio_element_params = {},
        }) {
    auto param_definition = std::make_unique<DemixingParamDefinition>();
    param_definition->parameter_id_ = 4;
    param_definition->parameter_rate_ = 5;
    param_definition->param_definition_mode_ = false;
    param_definition->reserved_ = 0;
    param_definition->duration_ = 64;
    param_definition->constant_subblock_duration_ = 64;
    param_definition->default_demixing_info_parameter_data_.dmixp_mode =
        DemixingInfoParameterData::kDMixPMode1;
    param_definition->default_demixing_info_parameter_data_.reserved = 0;
    param_definition->default_demixing_info_parameter_data_.default_w = 0;
    param_definition->default_demixing_info_parameter_data_.reserved_default =
        0;

    required_args_.audio_element_params.push_back(
        AudioElementParam{ParamDefinition::kParameterDefinitionDemixing,
                          std::move(param_definition)});
  }

  ~AudioElementObuTestBase() = default;

 protected:
  void InitExpectOk() override {
    InitMainAudioElementObu();
    InitAudioElementTypeSpecificFields();
  }

  virtual void InitAudioElementTypeSpecificFields() = 0;

  void WriteObuExpectOk(WriteBitBuffer& wb) override {
    EXPECT_TRUE(obu_->ValidateAndWriteObu(wb).ok());
  }

  std::unique_ptr<AudioElementObu> obu_;
  RequiredAudioElementArgs required_args_;

 private:
  void InitMainAudioElementObu() {
    obu_ = std::make_unique<AudioElementObu>(
        header_, required_args_.audio_element_id,
        required_args_.audio_element_type, required_args_.reserved,
        required_args_.codec_config_id);

    // Create the Audio Substream IDs array. Loop to populate it.
    obu_->InitializeAudioSubstreams(required_args_.num_substreams);
    obu_->audio_substream_ids_ = required_args_.substream_ids;

    // Create the Audio Parameters array. Loop to populate it.
    obu_->InitializeParams(required_args_.num_parameters);
    for (int i = 0; i < required_args_.num_parameters; ++i) {
      obu_->audio_element_params_[i] =
          std::move(required_args_.audio_element_params[i]);
    }
  }
};

class AudioElementScalableChannelTest : public AudioElementObuTestBase,
                                        public testing::Test {
 public:
  struct ScalableChannelArguments {
    uint8_t num_layers;
    uint8_t scalable_channel_config_reserved;

    // All below vectors have a length of `num_layers`.
    std::vector<ChannelAudioLayerConfig::LoudspeakerLayout>
        loud_speaker_layouts;
    std::vector<uint8_t> output_gain_is_present_flag;
    std::vector<uint8_t> recon_gain_is_present_flag;
    std::vector<uint8_t> reserved_a;
    std::vector<uint8_t> substream_count;
    std::vector<uint8_t> coupled_substream_count;
    std::vector<uint8_t> output_gain_flag;
    std::vector<uint8_t> reserved_b;
    std::vector<int16_t> output_gain;
  };

  AudioElementScalableChannelTest()
      : AudioElementObuTestBase(AudioElementObu::kAudioElementChannelBased),
        scalable_channel_arguments_(
            {.num_layers = 1,
             .scalable_channel_config_reserved = 0,
             .loud_speaker_layouts = {ChannelAudioLayerConfig::kLayoutStereo},
             .output_gain_is_present_flag = {1},
             .recon_gain_is_present_flag = {1},
             .reserved_a = {0},
             .substream_count = {1},
             .coupled_substream_count = {1},
             .output_gain_flag = {1},
             .reserved_b = {0},
             .output_gain = {1}}) {}

 protected:
  void InitLayers() {
    const uint8_t num_layers = scalable_channel_arguments_.num_layers;
    scalable_channel_arguments_.num_layers = num_layers;
    // Overwrite all variable-sized vectors with default data of a length
    // implied by the default argument.
    scalable_channel_arguments_.loud_speaker_layouts =
        std::vector<ChannelAudioLayerConfig::LoudspeakerLayout>(
            num_layers, ChannelAudioLayerConfig::kLayoutStereo);
    scalable_channel_arguments_.output_gain_is_present_flag =
        std::vector<uint8_t>(num_layers, 1);
    scalable_channel_arguments_.recon_gain_is_present_flag =
        std::vector<uint8_t>(num_layers, 1);
    scalable_channel_arguments_.reserved_a =
        std::vector<uint8_t>(num_layers, 0);
    scalable_channel_arguments_.substream_count =
        std::vector<uint8_t>(num_layers, 1);
    scalable_channel_arguments_.coupled_substream_count =
        std::vector<uint8_t>(num_layers, 1);
    scalable_channel_arguments_.output_gain_flag =
        std::vector<uint8_t>(num_layers, 1);
    scalable_channel_arguments_.reserved_b =
        std::vector<uint8_t>(num_layers, 0);
    scalable_channel_arguments_.output_gain =
        std::vector<int16_t>(num_layers, 1);
  }

  void InitSubstreamIds() {
    // Overwrite the variable-sized `substream_ids` array with default data of a
    // length implied by the default argument.
    required_args_.substream_ids =
        std::vector<DecodedUleb128>(required_args_.num_substreams);
    std::iota(required_args_.substream_ids.begin(),
              required_args_.substream_ids.end(), 1);
  }

  void InitAudioElementTypeSpecificFields() override {
    EXPECT_TRUE(
        obu_->InitializeScalableChannelLayout(
                scalable_channel_arguments_.num_layers,
                scalable_channel_arguments_.scalable_channel_config_reserved)
            .ok());

    auto& config = std::get<ScalableChannelLayoutConfig>(obu_->config_);
    for (int i = 0; i < config.num_layers; ++i) {
      ChannelAudioLayerConfig& layer_config =
          config.channel_audio_layer_configs[i];
      layer_config.loudspeaker_layout =
          scalable_channel_arguments_.loud_speaker_layouts[i];
      layer_config.output_gain_is_present_flag =
          scalable_channel_arguments_.output_gain_is_present_flag[i];
      layer_config.recon_gain_is_present_flag =
          scalable_channel_arguments_.recon_gain_is_present_flag[i];
      layer_config.reserved_a = scalable_channel_arguments_.reserved_a[i];
      layer_config.substream_count =
          scalable_channel_arguments_.substream_count[i];
      layer_config.coupled_substream_count =
          scalable_channel_arguments_.coupled_substream_count[i];
      layer_config.output_gain_flag =
          scalable_channel_arguments_.output_gain_flag[i];
      layer_config.reserved_b = scalable_channel_arguments_.reserved_b[i];
      layer_config.output_gain = scalable_channel_arguments_.output_gain[i];
    }
  }

  ScalableChannelArguments scalable_channel_arguments_;
};

TEST_F(AudioElementScalableChannelTest, ConstructSetsObuType) {
  InitExpectOk();
  EXPECT_EQ(obu_->header_.obu_type, kObuIaAudioElement);
}

TEST_F(AudioElementScalableChannelTest, Default) {
  expected_payload_ = {
      // `audio_element_id`.
      1,
      // `audio_element_type (3), reserved (5).
      AudioElementObu::kAudioElementChannelBased << 5,
      // `codec_config_id`.
      2,
      // `num_substreams`.
      1,
      // `audio_substream_ids`
      3,
      // `num_parameters`.
      1,
      // `audio_element_params[0]`.
      kParameterDefinitionDemixingAsUint8, 4, 5, 0x00, 64, 64, 0, 0,
      // `scalable_channel_layout_config`.
      // `num_layers` (3), reserved (5).
      1 << 5,
      // `channel_audio_layer_config[0]`.
      // `loudspeaker_layout` (4), `output_gain_is_present_flag` (1),
      // `recon_gain_is_present_flag` (1), `reserved` (2).
      ChannelAudioLayerConfig::kLayoutStereo << 4 | (1 << 3) | (1 << 2),
      // `substream_count`.
      1,
      // `coupled_substream_count`.
      1,
      // `output_gain_flags` (6) << reserved.
      1 << 2,
      // `output_gain`.
      0, 1};
  InitAndTestWrite();
}

TEST_F(AudioElementScalableChannelTest, RedundantCopy) {
  header_.obu_redundant_copy = true;
  expected_header_ = {kObuIaAudioElement << 3 | kObuRedundantCopyBitMask, 21};
  expected_payload_ = {
      // `audio_element_id`.
      1,
      // `audio_element_type (3), reserved (5).
      AudioElementObu::kAudioElementChannelBased << 5,
      // `codec_config_id`.
      2,
      // `num_substreams`.
      1,
      // `audio_substream_ids`
      3,
      // `num_parameters`.
      1,
      // `audio_element_params[0]`.
      kParameterDefinitionDemixingAsUint8, 4, 5, 0x00, 64, 64, 0, 0,
      // `scalable_channel_layout_config`.
      // `num_layers` (3), reserved (5).
      1 << 5,
      // `channel_audio_layer_config[0]`.
      // `loudspeaker_layout` (4), `output_gain_is_present_flag` (1),
      // `recon_gain_is_present_flag` (1), `reserved` (2).
      ChannelAudioLayerConfig::kLayoutStereo << 4 | (1 << 3) | (1 << 2),
      // `substream_count`.
      1,
      // `coupled_substream_count`.
      1,
      // `output_gain_flags` (6) << reserved.
      1 << 2,
      // `output_gain`.
      0, 1};
  InitAndTestWrite();
}

TEST_F(AudioElementScalableChannelTest,
       ValidateAndWriteFailsWithInvalidObuTrimmingStatusFlag) {
  header_.obu_trimming_status_flag = true;

  InitExpectOk();
  WriteBitBuffer unused_wb(0);
  EXPECT_FALSE(obu_->ValidateAndWriteObu(unused_wb).ok());
}

TEST_F(AudioElementScalableChannelTest,
       ValidateAndWriteFailsWithInvalidNumSubstreams) {
  required_args_.num_substreams = 0;
  required_args_.substream_ids = {};

  InitExpectOk();
  WriteBitBuffer unused_wb(0);
  EXPECT_FALSE(obu_->ValidateAndWriteObu(unused_wb).ok());
}

TEST_F(AudioElementScalableChannelTest,
       ValidateAndWriteFailsWithInvalidParameterDefinitionMixGain) {
  required_args_.audio_element_params[0].param_definition_type =
      ParamDefinition::kParameterDefinitionMixGain;

  InitExpectOk();
  WriteBitBuffer unused_wb(0);
  EXPECT_FALSE(obu_->ValidateAndWriteObu(unused_wb).ok());
}

TEST_F(AudioElementScalableChannelTest, ParamDefinitionExtensionZero) {
  required_args_.audio_element_params[0] = {
      ParamDefinition::kParameterDefinitionReservedStart,
      std::make_unique<ExtendedParamDefinition>(
          ParamDefinition::kParameterDefinitionReservedStart)};

  expected_header_ = {kObuIaAudioElement << 3, 15};

  expected_payload_ = {
      // `audio_element_id`.
      1,
      // `audio_element_type (3), reserved (5).
      AudioElementObu::kAudioElementChannelBased << 5,
      // `codec_config_id`.
      2,
      // `num_substreams`.
      1,
      // `audio_substream_ids`
      3,
      // `num_parameters`.
      1,
      // `audio_element_params[0]`.
      3, 0,
      // `scalable_channel_layout_config`.
      // `num_layers` (3), reserved (5).
      1 << 5,
      // `channel_audio_layer_config[0]`.
      // `loudspeaker_layout` (4), `output_gain_is_present_flag` (1),
      // `recon_gain_is_present_flag` (1), `reserved` (2).
      ChannelAudioLayerConfig::kLayoutStereo << 4 | (1 << 3) | (1 << 2),
      // `substream_count`.
      1,
      // `coupled_substream_count`.
      1,
      // `output_gain_flags` (6) << reserved.
      1 << 2,
      // `output_gain`.
      0, 1};

  InitAndTestWrite();
}

TEST_F(AudioElementScalableChannelTest, MaxParamDefinitionType) {
  required_args_.audio_element_params[0] = {
      ParamDefinition::kParameterDefinitionReservedEnd,
      std::make_unique<ExtendedParamDefinition>(
          ParamDefinition::kParameterDefinitionReservedEnd)};

  expected_header_ = {kObuIaAudioElement << 3, 19};

  expected_payload_ = {
      // `audio_element_id`.
      1,
      // `audio_element_type (3), reserved (5).
      AudioElementObu::kAudioElementChannelBased << 5,
      // `codec_config_id`.
      2,
      // `num_substreams`.
      1,
      // `audio_substream_ids`
      3,
      // `num_parameters`.
      1,
      // `audio_element_params[0]`.
      0xff, 0xff, 0xff, 0xff, 0x0f, 0,
      // `scalable_channel_layout_config`.
      // `num_layers` (3), reserved (5).
      1 << 5,
      // `channel_audio_layer_config[0]`.
      // `loudspeaker_layout` (4), `output_gain_is_present_flag` (1),
      // `recon_gain_is_present_flag` (1), `reserved` (2).
      ChannelAudioLayerConfig::kLayoutStereo << 4 | (1 << 3) | (1 << 2),
      // `substream_count`.
      1,
      // `coupled_substream_count`.
      1,
      // `output_gain_flags` (6) << reserved.
      1 << 2,
      // `output_gain`.
      0, 1};

  InitAndTestWrite();
}

TEST_F(AudioElementScalableChannelTest, ParamDefinitionExtensionNonZero) {
  auto param_definition = std::make_unique<ExtendedParamDefinition>(
      ParamDefinition::kParameterDefinitionReservedStart);
  param_definition->param_definition_size_ = 5;
  param_definition->param_definition_bytes_ = {'e', 'x', 't', 'r', 'a'};

  required_args_.audio_element_params[0] = {
      ParamDefinition::kParameterDefinitionReservedStart,
      std::move(param_definition)};

  expected_header_ = {kObuIaAudioElement << 3, 20};

  expected_payload_ = {
      // `audio_element_id`.
      1,
      // `audio_element_type (3), reserved (5).
      AudioElementObu::kAudioElementChannelBased << 5,
      // `codec_config_id`.
      2,
      // `num_substreams`.
      1,
      // `audio_substream_ids`
      3,
      // `num_parameters`.
      1,
      // `audio_element_params[0]`.
      3, 5, 'e', 'x', 't', 'r', 'a',
      // `scalable_channel_layout_config`.
      // `num_layers` (3), reserved (5).
      1 << 5,
      // `channel_audio_layer_config[0]`.
      // `loudspeaker_layout` (4), `output_gain_is_present_flag` (1),
      // `recon_gain_is_present_flag` (1), `reserved` (2).
      ChannelAudioLayerConfig::kLayoutStereo << 4 | (1 << 3) | (1 << 2),
      // `substream_count`.
      1,
      // `coupled_substream_count`.
      1,
      // `output_gain_flags` (6) << reserved.
      1 << 2,
      // `output_gain`.
      0, 1};

  InitAndTestWrite();
}

const ScalableChannelLayoutConfig kTwoLayerStereoConfig = {
    .num_layers = 2,
    .channel_audio_layer_configs = {
        ChannelAudioLayerConfig{
            .loudspeaker_layout = ChannelAudioLayerConfig::kLayoutMono,
            .output_gain_is_present_flag = false,
            .recon_gain_is_present_flag = false,
            .substream_count = 1,
            .coupled_substream_count = 0},
        ChannelAudioLayerConfig{
            .loudspeaker_layout = ChannelAudioLayerConfig::kLayoutStereo,
            .output_gain_is_present_flag = false,
            .recon_gain_is_present_flag = false,
            .substream_count = 1,
            .coupled_substream_count = 0}}};
const DecodedUleb128 kTwoLayerStereoSubstreamCount = 2;

TEST(ScalableChannelLayoutConfigValidate, IsOkWithMultipleLayers) {
  EXPECT_TRUE(
      kTwoLayerStereoConfig.Validate(kTwoLayerStereoSubstreamCount).ok());
}

TEST(ScalableChannelLayoutConfigValidate,
     IsNotOkWhenSubstreamCountDoesNotMatchWithMultipleLayers) {
  EXPECT_FALSE(
      kTwoLayerStereoConfig.Validate(kTwoLayerStereoSubstreamCount + 1).ok());
}

TEST(ScalableChannelLayoutConfigValidate, TooFewLayers) {
  const ScalableChannelLayoutConfig kConfigWithZeroLayer = {.num_layers = 0};

  EXPECT_FALSE(kConfigWithZeroLayer.Validate(0).ok());
}

TEST(ScalableChannelLayoutConfigValidate, TooManyLayers) {
  const ScalableChannelLayoutConfig kConfigWithZeroLayer = {
      .num_layers = 7,
      .channel_audio_layer_configs = std::vector<ChannelAudioLayerConfig>(7)};

  EXPECT_FALSE(kConfigWithZeroLayer.Validate(0).ok());
}

const ChannelAudioLayerConfig kChannelAudioLayerConfigBinaural = {
    .loudspeaker_layout = ChannelAudioLayerConfig::kLayoutBinaural,
    .output_gain_is_present_flag = false,
    .recon_gain_is_present_flag = false,
    .substream_count = 1,
    .coupled_substream_count = 1};

const ChannelAudioLayerConfig kChannelAudioLayerConfigStereo = {
    .loudspeaker_layout = ChannelAudioLayerConfig::kLayoutStereo,
    .output_gain_is_present_flag = false,
    .recon_gain_is_present_flag = false,
    .substream_count = 1,
    .coupled_substream_count = 1};

TEST(ScalableChannelLayoutConfigValidate, IsOkWithOneLayerBinaural) {
  const ScalableChannelLayoutConfig kBinauralConfig = {
      .num_layers = 1,
      .channel_audio_layer_configs = {kChannelAudioLayerConfigBinaural}};

  EXPECT_TRUE(kBinauralConfig.Validate(1).ok());
}

TEST(ScalableChannelLayoutConfigValidate,
     MustHaveExactlyOneLayerIfBinauralIsPresent) {
  const ScalableChannelLayoutConfig kInvalidBinauralConfigWithFirstLayerStereo =
      {.num_layers = 2,
       .channel_audio_layer_configs = {kChannelAudioLayerConfigStereo,
                                       kChannelAudioLayerConfigBinaural}};
  const ScalableChannelLayoutConfig
      kInvalidBinauralConfigWithSecondLayerStereo = {
          .num_layers = 2,
          .channel_audio_layer_configs = {kChannelAudioLayerConfigBinaural,
                                          kChannelAudioLayerConfigStereo}};

  EXPECT_FALSE(kInvalidBinauralConfigWithFirstLayerStereo.Validate(2).ok());
  EXPECT_FALSE(kInvalidBinauralConfigWithSecondLayerStereo.Validate(2).ok());
}

TEST_F(AudioElementScalableChannelTest, TwoSubstreams) {
  required_args_.num_substreams = 2;
  scalable_channel_arguments_.substream_count = {2};
  InitSubstreamIds();

  expected_header_ = {kObuIaAudioElement << 3, 22};
  expected_payload_ = {
      1, AudioElementObu::kAudioElementChannelBased << 5, 2,
      // `num_substreams`.
      2,
      // `audio_substream_ids`.
      1, 2,
      // `num_parameters`.
      1, kParameterDefinitionDemixingAsUint8,
      // Start `DemixingParamDefinition`.
      4, 5, 0x00, 64, 64, 0, 0,
      // `scalable_channel_layout_config`.
      // `num_layers` (3), reserved (5).
      1 << 5,
      // `channel_audio_layer_config[0]`.
      // `loudspeaker_layout` (4), `output_gain_is_present_flag` (1),
      // `recon_gain_is_present_flag` (1), `reserved` (2).
      ChannelAudioLayerConfig::kLayoutStereo << 4 | (1 << 3) | (1 << 2),
      // `substream_count`.
      2,
      // `coupled_substream_count`.
      1,
      // `output_gain_flags` (6) << reserved.
      1 << 2,
      // `output_gain`.
      0, 1};
  InitAndTestWrite();
}

TEST_F(AudioElementScalableChannelTest,
       ValidateAndWriteFailsWithInvalidDuplicateParamDefinitionTypesExtension) {
  required_args_.num_parameters = 2;
  required_args_.audio_element_params.clear();
  const auto kDuplicateParameterDefinition =
      ParamDefinition::kParameterDefinitionReservedStart;

  required_args_.audio_element_params.emplace_back(AudioElementParam{
      kDuplicateParameterDefinition, std::make_unique<ExtendedParamDefinition>(
                                         kDuplicateParameterDefinition)});
  required_args_.audio_element_params.emplace_back(AudioElementParam{
      kDuplicateParameterDefinition, std::make_unique<ExtendedParamDefinition>(
                                         kDuplicateParameterDefinition)});

  InitExpectOk();
  WriteBitBuffer unused_wb(0);
  EXPECT_FALSE(obu_->ValidateAndWriteObu(unused_wb).ok());
}

TEST_F(AudioElementScalableChannelTest,
       ValidateAndWriteFailsWithInvalidDuplicateParamDefinitionTypesDemixing) {
  DemixingParamDefinition demixing_param_definition;
  demixing_param_definition.parameter_id_ = 4;
  demixing_param_definition.parameter_rate_ = 5;
  demixing_param_definition.param_definition_mode_ = false;
  demixing_param_definition.reserved_ = 0;
  demixing_param_definition.duration_ = 64;
  demixing_param_definition.constant_subblock_duration_ = 64;
  demixing_param_definition.default_demixing_info_parameter_data_.dmixp_mode =
      DemixingInfoParameterData::kDMixPMode1;
  demixing_param_definition.default_demixing_info_parameter_data_.reserved = 0;
  demixing_param_definition.default_demixing_info_parameter_data_.default_w = 0;
  demixing_param_definition.default_demixing_info_parameter_data_
      .reserved_default = 0;

  required_args_.num_parameters = 2;

  required_args_.audio_element_params.clear();
  for (int i = 0; i < 2; i++) {
    auto param_definition = std::make_unique<DemixingParamDefinition>();
    *param_definition = demixing_param_definition;
    required_args_.audio_element_params.emplace_back(
        AudioElementParam{ParamDefinition::kParameterDefinitionReservedStart,
                          std::move(param_definition)});
  }

  InitExpectOk();
  WriteBitBuffer unused_wb(0);
  EXPECT_FALSE(obu_->ValidateAndWriteObu(unused_wb).ok());
}

class AudioElementMonoAmbisonicsTest : public AudioElementObuTestBase,
                                       public testing::Test {
 public:
  struct AmbisonicsMonoArguments {
    DecodedUleb128 ambisonics_mode;

    AmbisonicsMonoConfig config;
  };

  AudioElementMonoAmbisonicsTest()
      : AudioElementObuTestBase(AudioElementObu::kAudioElementSceneBased),
        ambisonics_mono_arguments_(
            {AmbisonicsConfig::kAmbisonicsModeMono,
             AmbisonicsMonoConfig{.output_channel_count = 1,
                                  .substream_count = 1,
                                  .channel_mapping = {0}}}) {}

 protected:
  void InitSubstreamsAndChannelMapping() {
    required_args_.num_substreams =
        ambisonics_mono_arguments_.config.substream_count;
    required_args_.substream_ids = std::vector<DecodedUleb128>(
        ambisonics_mono_arguments_.config.substream_count);
    std::iota(required_args_.substream_ids.begin(),
              required_args_.substream_ids.end(), 0);

    // Overwrite the variable-sized `channel_mapping`  with default data of a
    // length implied by the default argument.
    ambisonics_mono_arguments_.config.channel_mapping = std::vector<uint8_t>(
        ambisonics_mono_arguments_.config.output_channel_count,
        AmbisonicsMonoConfig::kInactiveAmbisonicsChannelNumber);
    // Assign channels [0, substream_count - 1] in order. The remaining channels
    // (if any) represent dropped channels in mixed-order ambisonics.

    std::iota(ambisonics_mono_arguments_.config.channel_mapping.begin(),
              ambisonics_mono_arguments_.config.channel_mapping.begin() +
                  ambisonics_mono_arguments_.config.substream_count,
              0);
  }

  void InitAudioElementTypeSpecificFields() override {
    EXPECT_TRUE(obu_->InitializeAmbisonicsMono(
                        ambisonics_mono_arguments_.config.output_channel_count,
                        ambisonics_mono_arguments_.config.substream_count)
                    .ok());
    std::get<AmbisonicsMonoConfig>(
        std::get<AmbisonicsConfig>(obu_->config_).ambisonics_config) =
        ambisonics_mono_arguments_.config;
  }

  AmbisonicsMonoArguments ambisonics_mono_arguments_;
};

TEST_F(AudioElementMonoAmbisonicsTest, Default) {
  expected_header_ = {kObuIaAudioElement << 3, 18},
  expected_payload_ = {// `audio_element_id`.
                       1,
                       // `audio_element_type (3), reserved (5).
                       AudioElementObu::kAudioElementSceneBased << 5,
                       // `codec_config_id`.
                       2,
                       // `num_substreams`.
                       1,
                       // `audio_substream_ids`
                       3,
                       // `num_parameters`.
                       1,
                       // `audio_element_params[0]`.
                       kParameterDefinitionDemixingAsUint8, 4, 5, 0x00, 64, 64,
                       0, 0,
                       // Start `ambisonics_config`.
                       // `ambisonics_mode`.
                       AmbisonicsConfig::kAmbisonicsModeMono,
                       // `output_channel_count`.
                       1,
                       // `substream_count`.
                       1,
                       // `channel_mapping`.
                       0};
  InitAndTestWrite();
}

TEST_F(AudioElementMonoAmbisonicsTest,
       NonMinimalLebGeneratorAffectsAllLeb128s) {
  leb_generator_ =
      LebGenerator::Create(LebGenerator::GenerationMode::kFixedSize, 2);

  expected_header_ = {kObuIaAudioElement << 3, 0x80 | 29, 0x00},
  expected_payload_ = {
      // `audio_element_id` is affected by the `LebGenerator`.
      0x80 | 1, 0x00,
      // `audio_element_type (3), reserved (5).
      AudioElementObu::kAudioElementSceneBased << 5,
      // `codec_config_id` is affected by the `LebGenerator`.
      0x80 | 2, 0x00,
      // `num_substreams` is affected by the `LebGenerator`.
      0x80 | 1, 0x00,
      // `audio_substream_ids` is affected by the `LebGenerator`.
      0x80 | 3, 0x00,
      // `num_parameters`. is affected by the `LebGenerator`.
      0x80 | 1, 0x00,
      // `audio_element_params[0]`.
      // `param_definition_type` is affected by the `LebGenerator`.
      0x80 | kParameterDefinitionDemixingAsUint8, 0x00,
      // `parameter_id` is affected by the `LebGenerator`.
      0x80 | 4, 0x00,
      // `parameter_rate` is affected by the `LebGenerator`.
      0x80 | 5, 0x00, 0x00,
      // `duration` is affected by the `LebGenerator`.
      0x80 | 64, 0x00,
      // `constant_subblock_duration` is affected by the `LebGenerator`.
      0x80 | 64, 0x00, 0, 0,
      // Start `ambisonics_config`.
      // `ambisonics_mode` is affected by the `LebGenerator`.
      0x80 | AmbisonicsConfig::kAmbisonicsModeMono, 0x00,
      // `output_channel_count`.
      1,
      // `substream_count`.
      1,
      // `channel_mapping`.
      0};
  InitAndTestWrite();
}

TEST_F(AudioElementMonoAmbisonicsTest, FoaAmbisonicsMono) {
  ambisonics_mono_arguments_.config.output_channel_count = 4;
  ambisonics_mono_arguments_.config.substream_count = 4;
  InitSubstreamsAndChannelMapping();

  expected_header_ = {kObuIaAudioElement << 3, 24},
  expected_payload_ =
      {// `audio_element_id`.
       1,
       // `audio_element_type (3), reserved (5).
       AudioElementObu::kAudioElementSceneBased << 5,
       // `codec_config_id`.
       2,
       // `num_substreams`.
       4,
       // `audio_substream_ids`
       0, 1, 2, 3,
       // `num_parameters`.
       1,
       // `audio_element_params[0]`.
       kParameterDefinitionDemixingAsUint8, 4, 5, 0x00, 64, 64, 0, 0,
       // Start `ambisonics_config`.
       // `ambisonics_mode`.
       AmbisonicsConfig::kAmbisonicsModeMono,
       // `output_channel_count`.
       4,
       // `substream_count`.
       4,
       // `channel_mapping`.
       0, 1, 2, 3},
  InitAndTestWrite();
}

TEST_F(AudioElementMonoAmbisonicsTest, MaxAmbisonicsMono) {
  ambisonics_mono_arguments_.config.output_channel_count = 225;
  ambisonics_mono_arguments_.config.substream_count = 225;
  InitSubstreamsAndChannelMapping();

  // The actual OBU would be verbose. Just validate the size of the write
  // matches expectations.

  expected_header_ = std::vector<uint8_t>(3),
  expected_payload_ = std::vector<uint8_t>(564),
  InitAndTestWrite(/*only_validate_size=*/true);
}

class AudioElementProjAmbisonicsTest : public AudioElementObuTestBase,
                                       public testing::Test {
 public:
  struct AmbisonicsProjArguments {
    DecodedUleb128 ambisonics_mode;
    AmbisonicsProjectionConfig config;
  };

  AudioElementProjAmbisonicsTest()
      : AudioElementObuTestBase(AudioElementObu::kAudioElementSceneBased),
        ambisonics_proj_arguments_(
            {AmbisonicsConfig::kAmbisonicsModeProjection,
             AmbisonicsProjectionConfig{.output_channel_count = 1,
                                        .substream_count = 1,
                                        .coupled_substream_count = 0,
                                        .demixing_matrix = {1}}}) {}

 protected:
  void InitSubstreamsAndDemixingMatrix() {
    required_args_.num_substreams =
        ambisonics_proj_arguments_.config.substream_count;
    required_args_.substream_ids = std::vector<DecodedUleb128>(
        ambisonics_proj_arguments_.config.substream_count);
    std::iota(required_args_.substream_ids.begin(),
              required_args_.substream_ids.end(), 0);

    const size_t demixing_matrix_size =
        ambisonics_proj_arguments_.config.substream_count *
        ambisonics_proj_arguments_.config.output_channel_count;

    // Overwrite the variable-sized `demixing_matrix` channel_mapping  with
    // default data of a length implied by the default argument.
    ambisonics_proj_arguments_.config.demixing_matrix =
        std::vector<int16_t>(demixing_matrix_size, 0);
    std::iota(ambisonics_proj_arguments_.config.demixing_matrix.begin(),
              ambisonics_proj_arguments_.config.demixing_matrix.end(), 1);
  }

  void InitAudioElementTypeSpecificFields() {
    EXPECT_TRUE(
        obu_->InitializeAmbisonicsProjection(
                ambisonics_proj_arguments_.config.output_channel_count,
                ambisonics_proj_arguments_.config.substream_count,
                ambisonics_proj_arguments_.config.coupled_substream_count)
            .ok());

    std::get<AmbisonicsProjectionConfig>(
        std::get<AmbisonicsConfig>(obu_->config_).ambisonics_config) =
        ambisonics_proj_arguments_.config;
  }

  AmbisonicsProjArguments ambisonics_proj_arguments_;
};

TEST_F(AudioElementProjAmbisonicsTest, Default) {
  expected_header_ = {kObuIaAudioElement << 3, 20};
  expected_payload_ = {// `audio_element_id`.
                       1,
                       // `audio_element_type (3), reserved (5).
                       AudioElementObu::kAudioElementSceneBased << 5,
                       // `codec_config_id`.
                       2,
                       // `num_substreams`.
                       1,
                       // `audio_substream_ids`
                       3,
                       // `num_parameters`.
                       1,
                       // `audio_element_params[0]`.
                       kParameterDefinitionDemixingAsUint8, 4, 5, 0x00, 64, 64,
                       0, 0,
                       // Start `ambisonics_config`.
                       // `ambisonics_mode`.
                       AmbisonicsConfig::kAmbisonicsModeProjection,
                       // `output_channel_count`.
                       1,
                       // `substream_count`.
                       1,
                       // `coupled_substream_count`.
                       0,
                       // `demixing_matrix`.
                       /*             ACN#:    0*/
                       /* Substream   0: */ 0, 1};
  InitAndTestWrite();
}

TEST_F(AudioElementProjAmbisonicsTest, FoaAmbisonicsOutputChannelCount) {
  ambisonics_proj_arguments_.config.output_channel_count = 4;
  ambisonics_proj_arguments_.config.substream_count = 4;
  InitSubstreamsAndDemixingMatrix();

  expected_header_ = {kObuIaAudioElement << 3, 53},
  expected_payload_ =
      {// `audio_element_id`.
       1,
       // `audio_element_type (3), reserved (5).
       AudioElementObu::kAudioElementSceneBased << 5,
       // `codec_config_id`.
       2,
       // `num_substreams`.
       4,
       // `audio_substream_ids`
       0, 1, 2, 3,
       // `num_parameters`.
       1,
       // `audio_element_params[0]`.
       kParameterDefinitionDemixingAsUint8, 4, 5, 0x00, 64, 64, 0, 0,
       // Start `ambisonics_config`.
       // `ambisonics_mode`.
       AmbisonicsConfig::kAmbisonicsModeProjection,
       // `output_channel_count`.
       4,
       // `substream_count`.
       4,
       // `coupled_substream_count`.
       0,
       // `demixing_matrix`.
       /*             ACN#:    0,    1,    2,    3 */
       /* Substream   0: */ 0, 1, 0, 2, 0, 3, 0, 4,
       /* Substream   1: */ 0, 5, 0, 6, 0, 7, 0, 8,
       /* Substream   2: */ 0, 9, 0, 10, 0, 11, 0, 12,
       /* Substream   3: */ 0, 13, 0, 14, 0, 15, 0, 16},
  InitAndTestWrite();
}

TEST_F(AudioElementProjAmbisonicsTest, MaxAmbisonicsOutputChannelCount) {
  ambisonics_proj_arguments_.config.output_channel_count = 225;
  ambisonics_proj_arguments_.config.substream_count = 225;
  InitSubstreamsAndDemixingMatrix();
  // The actual OBU would be verbose. Just validate the size of the write
  // matches expectations.

  expected_header_ = std::vector<uint8_t>(4);
  expected_payload_ = std::vector<uint8_t>(101590);
  InitAndTestWrite(/*only_validate_size=*/true);
}

class AudioElementExtensionConfigTest : public AudioElementObuTestBase,
                                        public testing::Test {
 public:
  AudioElementExtensionConfigTest()
      : AudioElementObuTestBase(AudioElementObu::kAudioElementBeginReserved),
        extension_config_({.audio_element_config_size = 0,
                           .audio_element_config_bytes = {}}) {
    expected_header_ = {kObuIaAudioElement << 3, 15};
  }

 protected:
  void InitAudioElementTypeSpecificFields() override {
    obu_->InitializeExtensionConfig(
        extension_config_.audio_element_config_size);
    std::get<ExtensionConfig>(obu_->config_) = extension_config_;
  }

  ExtensionConfig extension_config_;
};

TEST_F(AudioElementExtensionConfigTest, ExtensionSizeZero) {
  expected_payload_ = {// `audio_element_id`.
                       1,
                       // `audio_element_type (3), reserved (5).
                       AudioElementObu::kAudioElementBeginReserved << 5,
                       // `codec_config_id`.
                       2,
                       // `num_substreams`.
                       1,
                       // `audio_substream_ids`
                       3,
                       // `num_parameters`.
                       1,
                       // `audio_element_params[0]`.
                       kParameterDefinitionDemixingAsUint8, 4, 5, 0x00, 64, 64,
                       0, 0,
                       // `audio_element_config_size`.
                       0};
  InitAndTestWrite();
}

TEST_F(AudioElementExtensionConfigTest, MaxAudioElementType) {
  required_args_.audio_element_type = AudioElementObu::kAudioElementEndReserved;
  expected_payload_ = {// `audio_element_id`.
                       1,
                       // `audio_element_type (3), reserved (5).
                       AudioElementObu::kAudioElementEndReserved << 5,
                       // `codec_config_id`.
                       2,
                       // `num_substreams`.
                       1,
                       // `audio_substream_ids`
                       3,
                       // `num_parameters`.
                       1,
                       // `audio_element_params[0]`.
                       kParameterDefinitionDemixingAsUint8, 4, 5, 0x00, 64, 64,
                       0, 0,
                       // `audio_element_config_size`.
                       0};
  InitAndTestWrite();
}

TEST_F(AudioElementExtensionConfigTest, ExtensionSizeNonzero) {
  extension_config_.audio_element_config_size = 5;
  extension_config_.audio_element_config_bytes = {'e', 'x', 't', 'r', 'a'};

  expected_header_ = {kObuIaAudioElement << 3, 20};
  expected_payload_ = {// `audio_element_id`.
                       1,
                       // `audio_element_type (3), reserved (5).
                       AudioElementObu::kAudioElementBeginReserved << 5,
                       // `codec_config_id`.
                       2,
                       // `num_substreams`.
                       1,
                       // `audio_substream_ids`
                       3,
                       // `num_parameters`.
                       1,
                       // `audio_element_params[0]`.
                       kParameterDefinitionDemixingAsUint8, 4, 5, 0x00, 64, 64,
                       0, 0,
                       // `audio_element_config_size`.
                       5,
                       // 'audio_element_config_bytes`.
                       'e', 'x', 't', 'r', 'a'};
  InitAndTestWrite();
}

TEST(TestValidateAmbisonicsMono, MappingInAscendingOrder) {
  // Users may map the Ambisonics Channel Number to substreams in numerical
  // order. (e.g. A0 to the zeroth substream, A1 to the first substream, ...).
  const auto& ambisonics_mono = AmbisonicsMonoConfig{
      .output_channel_count = 4,
      .substream_count = 4,
      .channel_mapping = {/*A0=*/0, /*A1=*/1, /*A2=*/2, /*A3=*/3}};
  EXPECT_TRUE(ambisonics_mono.Validate(4).ok());
}

TEST(TestValidateAmbisonicsMono, MappingInArbitraryOrder) {
  // Users may map the Ambisonics Channel Number to substreams in any order.
  const auto& ambisonics_mono = AmbisonicsMonoConfig{
      .output_channel_count = 4,
      .substream_count = 4,
      .channel_mapping = {/*A0=*/3, /*A1=*/1, /*A2=*/0, /*A3=*/2}};
  EXPECT_TRUE(ambisonics_mono.Validate(4).ok());
}

TEST(TestValidateAmbisonicsMono, MixedOrderAmbisonics) {
  // User may choose to map the Ambisonics Channel Number (ACN) to
  // `255` to drop that ACN (e.g. to drop A0 and A3).
  const auto& ambisonics_mono = AmbisonicsMonoConfig{
      .output_channel_count = 4,
      .substream_count = 2,
      .channel_mapping = {/*A0=*/255, /*A1=*/1, /*A2=*/0, /*A3=*/255}};
  EXPECT_TRUE(ambisonics_mono.Validate(2).ok());
}

TEST(TestValidateAmbisonicsMono,
     ManyAmbisonicsChannelNumbersMappedToOneSubstream) {
  // User may choose to map several Ambisonics Channel Numbers (ACNs) to
  // one substream (e.g. A0, A1, A2, A3 are all mapped to the zeroth substream).
  const auto& ambisonics_mono = AmbisonicsMonoConfig{
      .output_channel_count = 4,
      .substream_count = 1,
      .channel_mapping = {/*A0=*/0, /*A1=*/0, /*A2=*/0, /*A3=*/0}};
  EXPECT_TRUE(ambisonics_mono.Validate(1).ok());
}

TEST(TestValidateAmbisonicsMono,
     InvalidWhenObuSubstreamCountDoesNotEqualSubstreamCount) {
  const auto& ambisonics_mono = AmbisonicsMonoConfig{
      .output_channel_count = 4,
      .substream_count = 4,
      .channel_mapping = {/*A0=*/0, /*A1=*/1, /*A2=*/2, /*A3=*/3}};
  const DecodedUleb128 kInconsistentObuSubstreamCount = 3;
  EXPECT_FALSE(ambisonics_mono.Validate(kInconsistentObuSubstreamCount).ok());
}

TEST(TestValidateAmbisonicsMono,
     InvalidWhenChannelMappingIsLargerThanSubstreamCount) {
  const auto& ambisonics_mono = AmbisonicsMonoConfig{
      .output_channel_count = 4,
      .substream_count = 2,
      .channel_mapping = {/*A0=*/255 /*A1=*/, 1 /*A2=*/, 0 /*A3=*/}};
  EXPECT_FALSE(ambisonics_mono.Validate(2).ok());
}

TEST(TestValidateAmbisonicsMono, InvalidOutputChannelCount) {
  const auto& ambisonics_mono = AmbisonicsMonoConfig{
      .output_channel_count = 5,
      .substream_count = 5,
      .channel_mapping = {/*A0=*/0, /*A1=*/1, /*A2=*/2, /*A3=*/3, /*A4=*/4}};
  EXPECT_FALSE(ambisonics_mono.Validate(2).ok());
}

TEST(TestValidateAmbisonicsMono, InvalidWhenSubstreamIndexIsTooLarge) {
  const auto& ambisonics_mono = AmbisonicsMonoConfig{
      .output_channel_count = 4,
      .substream_count = 4,
      .channel_mapping = {/*A0=*/0, /*A1=*/1, /*A2=*/2, /*A3=*/4}};
  EXPECT_FALSE(ambisonics_mono.Validate(4).ok());
}

TEST(TestValidateAmbisonicsMono,
     InvalidWhenNoAmbisonicsChannelNumberIsMappedToASubstream) {
  // The OBU claims two associated substreams. But substream 1 is in limbo and
  // has no meaning because there are no Ambisonics Channel Numbers mapped to
  // it.
  const auto& ambisonics_mono = AmbisonicsMonoConfig{
      .output_channel_count = 4,
      .substream_count = 2,
      .channel_mapping = {/*A0=*/0, /*A1=*/0, /*A2=*/0, /*A3=*/0}};
  EXPECT_FALSE(ambisonics_mono.Validate(2).ok());
}

TEST(TestValidateAmbisonicsProjection, FOAWithMainDiagonalMatrix) {
  // Typical users MAY create a matrix with non-zero values on the main
  // diagonal and zeroes in other entries. This results in one Ambisonics
  // Channel Number (ACN) represented per substream.
  const AmbisonicsProjectionConfig ambisonics_projection = {
      .output_channel_count = 4,
      .substream_count = 4,
      .coupled_substream_count = 0,
      .demixing_matrix = {/*           ACN#: 0, 1, 2, 3 */
                          /* Substream 0: */ 1, 0, 0, 0,
                          /* Substream 1: */ 0, 1, 0, 0,
                          /* Substream 2: */ 0, 0, 1, 0,
                          /* Substream 3: */ 0, 0, 0, 1}};
  EXPECT_TRUE(ambisonics_projection.Validate(4).ok());
}

TEST(TestValidateAmbisonicsProjection, FOAWithArbitraryMatrix) {
  // Users MAY set arbitrary values anywhere in this matrix, but the size MUST
  // comply with the spec. This results in multiple Ambisonics Channel Numbers
  // (ACNs) per substream.
  const AmbisonicsProjectionConfig ambisonics_projection = {
      .output_channel_count = 4,
      .substream_count = 4,
      .coupled_substream_count = 0,
      .demixing_matrix = {/*           ACN#: 0, 1, 2, 3 */
                          /* Substream 0: */ 1, 2, 3, 4,
                          /* Substream 1: */ 2, 3, 4, 5,
                          /* Substream 2: */ 3, 4, 5, 6,
                          /* Substream 3: */ 4, 5, 6, 7}};
  EXPECT_TRUE(ambisonics_projection.Validate(4).ok());
}

TEST(TestValidateAmbisonicsProjection, ZerothOrderAmbisonics) {
  const AmbisonicsProjectionConfig ambisonics_projection = {
      .output_channel_count = 1,
      .substream_count = 1,
      .coupled_substream_count = 0,
      .demixing_matrix = {
          /*                                             ACN#: 0, */
          /* Substream 0: */ std::numeric_limits<int16_t>::max()}};
  EXPECT_TRUE(ambisonics_projection.Validate(1).ok());
}

TEST(TestValidateAmbisonicsProjection, FOAWithOnlyA2) {
  // Fewer substreams than `output_channel_count` are allowed.
  const AmbisonicsProjectionConfig ambisonics_projection = {
      .output_channel_count = 4,
      .substream_count = 1,
      .coupled_substream_count = 0,
      .demixing_matrix = {/*           ACN#: 0, 1, 2, 3 */
                          /* Substream 0: */ 0, 0, 1, 0}};
  EXPECT_TRUE(ambisonics_projection.Validate(1).ok());
}

TEST(TestValidateAmbisonicsProjection, FOAOneCoupledStream) {
  // The first `coupled_substream_count` substreams are coupled. Each pair in
  // the coupling has a column in the bitstream (written as a row in this
  // test). The remaining streams are decoupled.
  const AmbisonicsProjectionConfig ambisonics_projection = {
      .output_channel_count = 4,
      .substream_count = 3,
      .coupled_substream_count = 1,
      .demixing_matrix = {/*             ACN#: 0, 1, 2, 3 */
                          /* Substream 0_a: */ 1, 0, 0, 0,
                          /* Substream 0_b: */ 0, 1, 0, 0,
                          /* Substream   1: */ 0, 0, 1, 0,
                          /* Substream   2: */ 0, 0, 0, 1}};
  EXPECT_TRUE(ambisonics_projection.Validate(3).ok());
}

TEST(TestValidateAmbisonicsProjection, FourteenthOrderAmbisonicsIsSupported) {
  const AmbisonicsProjectionConfig ambisonics_projection = {
      .output_channel_count = 225,
      .substream_count = 225,
      .coupled_substream_count = 0,
      .demixing_matrix = std::vector<int16_t>(225 * 225, 1)};
  EXPECT_TRUE(ambisonics_projection.Validate(225).ok());
}

TEST(TestValidateAmbisonicsProjection,
     FourteenthOrderAmbisonicsWithCoupledSubstreamsIsSupported) {
  const AmbisonicsProjectionConfig ambisonics_projection = {
      .output_channel_count = 225,
      .substream_count = 113,
      .coupled_substream_count = 112,
      .demixing_matrix = std::vector<int16_t>((113 + 112) * 225, 1)};
  EXPECT_TRUE(ambisonics_projection.Validate(113).ok());
}

TEST(TestValidateAmbisonicsProjection, InvalidOutputChannelCountMaxValue) {
  const AmbisonicsProjectionConfig ambisonics_projection = {
      .output_channel_count = 255,
      .substream_count = 255,
      .coupled_substream_count = 0,
      .demixing_matrix = std::vector<int16_t>(255 * 255, 1)};
  EXPECT_FALSE(ambisonics_projection.Validate(255).ok());
}

TEST(TestValidateAmbisonicsProjection, InvalidOutputChannelCount) {
  const AmbisonicsProjectionConfig ambisonics_projection = {
      .output_channel_count = 3,
      .substream_count = 3,
      .coupled_substream_count = 0,
      .demixing_matrix = std::vector<int16_t>(3 * 3, 1)};
  EXPECT_FALSE(ambisonics_projection.Validate(3).ok());
}

TEST(TestValidateAmbisonicsProjection,
     InvalidWhenSubstreamCountIsGreaterThanOutputChannelCount) {
  const AmbisonicsProjectionConfig ambisonics_projection = {
      .output_channel_count = 4,
      .substream_count = 5,
      .coupled_substream_count = 0,
      .demixing_matrix = std::vector<int16_t>(4 * 5, 1)};
  EXPECT_FALSE(ambisonics_projection.Validate(5).ok());
}

TEST(TestValidateAmbisonicsProjection,
     InvalidWhenObuSubstreamCountDoesNotEqualSubstreamCount) {
  const AmbisonicsProjectionConfig ambisonics_projection = {
      .output_channel_count = 4,
      .substream_count = 4,
      .coupled_substream_count = 0,
      .demixing_matrix = std::vector<int16_t>(4 * 4, 1)};
  const DecodedUleb128 kInconsistentObuSubstreamCount = 3;

  EXPECT_FALSE(
      ambisonics_projection.Validate(kInconsistentObuSubstreamCount).ok());
}

TEST(TestValidateAmbisonicsProjection,
     InvalidWhenCoupledSubstreamCountIsGreaterThanSubstreamCount) {
  const AmbisonicsProjectionConfig ambisonics_projection = {
      .output_channel_count = 4,
      .substream_count = 1,
      .coupled_substream_count = 3,
      .demixing_matrix = std::vector<int16_t>((1 + 3) * 4, 1)};

  EXPECT_FALSE(ambisonics_projection.Validate(1).ok());
}

TEST(TestValidateAmbisonicsProjection,
     InvalidWhenSubstreamCountPlusCoupledSubstreamCountIsTooLarge) {
  const AmbisonicsProjectionConfig ambisonics_projection = {
      .output_channel_count = 4,
      .substream_count = 3,
      .coupled_substream_count = 2,
      .demixing_matrix = std::vector<int16_t>((3 + 2) * 4, 1)};

  EXPECT_FALSE(ambisonics_projection.Validate(3).ok());
}

TEST(TestGetNextValidCount, ReturnsNextHighestCount) {
  uint8_t next_valid_count;
  EXPECT_TRUE(
      AmbisonicsConfig::GetNextValidOutputChannelCount(0, next_valid_count)
          .ok());
  EXPECT_EQ(next_valid_count, 1);
}

TEST(TestGetNextValidCount, SupportsFirstOrderAmbisonics) {
  uint8_t next_valid_count;
  EXPECT_TRUE(
      AmbisonicsConfig::GetNextValidOutputChannelCount(4, next_valid_count)
          .ok());
  EXPECT_EQ(next_valid_count, 4);
}

TEST(TestGetNextValidCount, SupportsFourteenthOrderAmbisonics) {
  uint8_t next_valid_count;
  EXPECT_TRUE(
      AmbisonicsConfig::GetNextValidOutputChannelCount(225, next_valid_count)
          .ok());
  EXPECT_EQ(next_valid_count, 225);
}

TEST(TestGetNextValidCount, InvalidInputTooLarge) {
  uint8_t unused_next_valid_count;
  EXPECT_FALSE(AmbisonicsConfig::GetNextValidOutputChannelCount(
                   226, unused_next_valid_count)
                   .ok());
}

// --- Begin CreateFromBuffer tests ---
// TODO(b/329700768): Update test once ValidateAndReadPayload is implemented.
TEST(CreateFromBuffer, IsNotSupported) {
  std::vector<uint8_t> source;
  ReadBitBuffer buffer(1024, &source);
  ObuHeader header;
  EXPECT_FALSE(AudioElementObu::CreateFromBuffer(header, buffer).ok());
}

TEST(CreateFromBuffer, ScalableChannelConfigMultipleChannelsNoParams) {
  std::vector<uint8_t> source = {
      // `audio_element_id`.
      1,
      // `audio_element_type (3), reserved (5).
      AudioElementObu::kAudioElementChannelBased << 5,
      // `codec_config_id`.
      2,
      // `num_substreams`.
      2,
      // `audio_substream_ids`
      3, 4,
      // `num_parameters`.
      0,
      // `scalable_channel_layout_config`.
      // `num_layers` (3), reserved (5).
      2 << 5,
      // `channel_audio_layer_config[0]`.
      // `loudspeaker_layout` (4), `output_gain_is_present_flag` (1),
      // `recon_gain_is_present_flag` (1), `reserved` (2).
      ChannelAudioLayerConfig::kLayoutStereo << 4 | (1 << 3) | (1 << 2),
      // `substream_count`.
      1,
      // `coupled_substream_count`.
      1,
      // `output_gain_flags` (6) << reserved.
      1 << 2,
      // `output_gain`.
      0, 1,
      // `channel_audio_layer_config[1]`.
      // `loudspeaker_layout` (4), `output_gain_is_present_flag` (1),
      // `recon_gain_is_present_flag` (1), `reserved` (2).
      ChannelAudioLayerConfig::kLayout5_1_ch << 4 | (1 << 3) | (1 << 2),
      // `substream_count`.
      1,
      // `coupled_substream_count`.
      1,
      // `output_gain_flags` (6) << reserved.
      1 << 2,
      // `output_gain`.
      0, 1};
  ReadBitBuffer buffer(1024, &source);
  ObuHeader header;
  auto obu = AudioElementObu::CreateFromBuffer(header, buffer);

  // Validate
  EXPECT_TRUE(obu.ok());
  EXPECT_EQ(obu.value().GetAudioElementId(), 1);
  EXPECT_EQ(obu.value().GetAudioElementType(),
            AudioElementObu::kAudioElementChannelBased);
  EXPECT_EQ(obu.value().num_substreams_, 2);
  EXPECT_EQ(obu.value().audio_substream_ids_[0], 3);
  EXPECT_EQ(obu.value().audio_substream_ids_[1], 4);
  EXPECT_EQ(obu.value().num_parameters_, 0);

  ScalableChannelLayoutConfig expected_scalable_channel_layout_config = {
      .num_layers = 2,
      .channel_audio_layer_configs = {
          ChannelAudioLayerConfig{
              .loudspeaker_layout = ChannelAudioLayerConfig::kLayoutStereo,
              .output_gain_is_present_flag = true,
              .recon_gain_is_present_flag = true,
              .substream_count = 1,
              .coupled_substream_count = 1,
              .output_gain_flag = 1,
              .reserved_b = 0,
              .output_gain = 1},
          ChannelAudioLayerConfig{
              .loudspeaker_layout = ChannelAudioLayerConfig::kLayout5_1_ch,
              .output_gain_is_present_flag = true,
              .recon_gain_is_present_flag = true,
              .substream_count = 1,
              .coupled_substream_count = 1,
              .output_gain_flag = 1,
              .reserved_b = 0,
              .output_gain = 1}}};
  EXPECT_EQ(std::get<ScalableChannelLayoutConfig>(obu.value().config_),
            expected_scalable_channel_layout_config);
}

TEST(CreateFromBuffer, InvalidMultipleChannelConfigWithBinauralLayout) {
  std::vector<uint8_t> source = {
      // `audio_element_id`.
      1,
      // `audio_element_type (3), reserved (5).
      AudioElementObu::kAudioElementChannelBased << 5,
      // `codec_config_id`.
      2,
      // `num_substreams`.
      2,
      // `audio_substream_ids`
      3, 4,
      // `num_parameters`.
      0,
      // `scalable_channel_layout_config`.
      // `num_layers` (3), reserved (5).
      2 << 5,
      // `channel_audio_layer_config[0]`.
      // `loudspeaker_layout` (4), `output_gain_is_present_flag` (1),
      // `recon_gain_is_present_flag` (1), `reserved` (2).
      ChannelAudioLayerConfig::kLayoutStereo << 4 | (1 << 3) | (1 << 2),
      // `substream_count`.
      1,
      // `coupled_substream_count`.
      1,
      // `output_gain_flags` (6) << reserved.
      1 << 2,
      // `output_gain`.
      0, 1,
      // `channel_audio_layer_config[1]`.
      // `loudspeaker_layout` (4), `output_gain_is_present_flag` (1),
      // `recon_gain_is_present_flag` (1), `reserved` (2).
      ChannelAudioLayerConfig::kLayoutBinaural << 4 | (0 << 3) | (0 << 2),
      // `substream_count`.
      1,
      // `coupled_substream_count`.
      1};
  ReadBitBuffer buffer(1024, &source);
  ObuHeader header;
  auto obu = AudioElementObu::CreateFromBuffer(header, buffer);

  EXPECT_FALSE(obu.ok());
}

}  // namespace
}  // namespace iamf_tools
