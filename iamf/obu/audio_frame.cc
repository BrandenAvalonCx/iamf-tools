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
#include "iamf/obu/audio_frame.h"

#include <cstdint>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "iamf/common/macros.h"
#include "iamf/common/read_bit_buffer.h"
#include "iamf/common/write_bit_buffer.h"
#include "iamf/obu/leb128.h"
#include "iamf/obu/obu_base.h"
#include "iamf/obu/obu_header.h"

namespace iamf_tools {

namespace {

ObuType GetObuType(uint32_t substream_id) {
  constexpr int kMaxImplicitAudioFrameID =
      kObuIaAudioFrameId17 - kObuIaAudioFrameId0;
  if (substream_id > kMaxImplicitAudioFrameID) {
    return kObuIaAudioFrame;
  }

  return static_cast<ObuType>(static_cast<uint32_t>(kObuIaAudioFrameId0) +
                              substream_id);
}

}  // namespace

AudioFrameObu::AudioFrameObu(const ObuHeader& header,
                             DecodedUleb128 substream_id,
                             const std::vector<uint8_t>& audio_frame)
    : ObuBase(header, GetObuType(substream_id)),
      audio_frame_(audio_frame),
      audio_substream_id_(substream_id) {}

absl::Status AudioFrameObu::ValidateAndWritePayload(WriteBitBuffer& wb) const {
  if (header_.obu_type == kObuIaAudioFrame) {
    // The ID is explicitly in the bitstream when `kObuIaAudioFrame`. Otherwise
    // it is implied by `obu_type`.
    RETURN_IF_NOT_OK(wb.WriteUleb128(audio_substream_id_));
  }
  RETURN_IF_NOT_OK(wb.WriteUint8Vector(audio_frame_));

  return absl::OkStatus();
}

absl::Status AudioFrameObu::ValidateAndReadPayload(ReadBitBuffer& rb) {
  return absl::UnimplementedError(
      "AudioFrameOBU ValidateAndReadPayload not yet implemented.");
}

void AudioFrameObu::PrintObu() const {
  LOG(INFO) << "  audio_substream_id= " << GetSubstreamId();
  LOG(INFO) << "  // samples_to_trim_at_end= "
            << header_.num_samples_to_trim_at_end;
  LOG(INFO) << "  // samples_to_trim_at_start= "
            << header_.num_samples_to_trim_at_start;
  LOG(INFO) << "  // size_of(audio_frame)= " << audio_frame_.size();
}

}  // namespace iamf_tools
