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
#ifndef CLI_AAC_ENCODER_DECODER_H_
#define CLI_AAC_ENCODER_DECODER_H_

#include <cstdint>
#include <memory>
#include <vector>

// This symbol conflicts with `aacenc_lib.h`.
#ifdef IS_LITTLE_ENDIAN
#undef IS_LITTLE_ENDIAN
#endif

#include "absl/status/status.h"
#include "iamf/cli/audio_frame_with_data.h"
#include "iamf/cli/codec/decoder_base.h"
#include "iamf/cli/codec/encoder_base.h"
#include "iamf/cli/proto/codec_config.pb.h"
#include "iamf/obu/codec_config.h"
#include "iamf/obu/decoder_config/aac_decoder_config.h"
#include "libAACdec/include/aacdecoder_lib.h"
#include "libAACenc/include/aacenc_lib.h"

namespace iamf_tools {

// TODO(b/277731089): Test all of `aac_encoder_decoder.h`.
class AacDecoder : public DecoderBase {
 public:
  /*\!brief Constructor.
   *
   * \param codec_config_obu Codec Config OBU with initialization settings.
   * \param num_channels Number of channels for this stream.
   */
  AacDecoder(const CodecConfigObu& codec_config_obu, int num_channels);

  /*\!brief Destructor.
   */
  ~AacDecoder() override;

  /*\!brief Initializes the underlying decoder.
   *
   * \return `absl::OkStatus()` on success. A specific status on failure.
   */
  absl::Status Initialize() override;

  /*\!brief Decodes an AAC audio frame.
   *
   * \param encoded_frame Frame to decode.
   * \param decoded_frames Output decoded frames arranged in (time, sample)
   *     axes.
   * \return `absl::OkStatus()` on success. A specific status on failure.
   */
  absl::Status DecodeAudioFrame(
      const std::vector<uint8_t>& encoded_frame,
      std::vector<std::vector<int32_t>>& decoded_frames) override;

 private:
  const AacDecoderConfig& aac_decoder_config_;
  AAC_DECODER_INSTANCE* decoder_ = nullptr;
};

class AacEncoder : public EncoderBase {
 public:
  AacEncoder(
      const iamf_tools_cli_proto::AacEncoderMetadata& aac_encoder_metadata,
      const CodecConfigObu& codec_config, int num_channels)
      : EncoderBase(false, codec_config, num_channels),
        encoder_metadata_(aac_encoder_metadata),
        decoder_config_(std::get<AacDecoderConfig>(
            codec_config.GetCodecConfig().decoder_config)) {}

  ~AacEncoder() override;

 private:
  /*!\brief Initializes the underlying encoder.
   *
   * \return `absl::OkStatus()` on success. A specific status on failure.
   */
  absl::Status InitializeEncoder() override;

  /*!\brief Initializes `required_samples_to_delay_at_start_`.
   *
   * `InitializeEncoder` is required to be called before calling this function.
   *
   * \return `absl::OkStatus()` on success. A specific status on failure.
   */
  absl::Status SetNumberOfSamplesToDelayAtStart() override;

  /*!\brief Encodes an audio frame.
   *
   * \param input_bit_depth Bit-depth of the input data.
   * \param samples Samples arranged in (time x channel) axes. The samples are
   *     left-justified and stored in the upper `input_bit_depth` bits.
   * \param partial_audio_frame_with_data Unique pointer to take ownership of.
   *     The underlying `audio_frame_` is modifed. All other fields are blindly
   *     passed along.
   * \return `absl::OkStatus()` on success. A specific status on failure.
   */
  absl::Status EncodeAudioFrame(
      int input_bit_depth, const std::vector<std::vector<int32_t>>& samples,
      std::unique_ptr<AudioFrameWithData> partial_audio_frame_with_data)
      override;

  const iamf_tools_cli_proto::AacEncoderMetadata encoder_metadata_;
  const AacDecoderConfig decoder_config_;

  // A pointer to the `fdk_aac` encoder.
  AACENCODER* encoder_ = nullptr;
};

}  // namespace iamf_tools

#endif  // CLI_AAC_ENCODER_DECODER_H_
