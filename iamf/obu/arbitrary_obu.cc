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
#include "iamf/obu/arbitrary_obu.h"

#include <cstdint>
#include <list>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "iamf/common/macros.h"
#include "iamf/common/read_bit_buffer.h"
#include "iamf/common/write_bit_buffer.h"

namespace iamf_tools {

absl::Status ArbitraryObu::WriteObusWithHook(
    InsertionHook insertion_hook, const std::list<ArbitraryObu>& arbitrary_obus,
    WriteBitBuffer& wb) {
  for (const auto& arbitrary_obu : arbitrary_obus) {
    if (arbitrary_obu.insertion_hook_ == insertion_hook) {
      RETURN_IF_NOT_OK(arbitrary_obu.ValidateAndWriteObu(wb));
    }
  }
  return absl::OkStatus();
}

absl::Status ArbitraryObu::ValidateAndWritePayload(WriteBitBuffer& wb) const {
  RETURN_IF_NOT_OK(wb.WriteUint8Vector(payload_));
  return absl::OkStatus();
}

absl::Status ArbitraryObu::ValidateAndReadPayload(ReadBitBuffer& rb) {
  return absl::UnimplementedError(
      "ArbitraryOBU ValidateAndReadPayload not yet implemented.");
}

void ArbitraryObu::PrintObu() const {
  LOG(INFO) << "Arbitrary OBU:";
  LOG(INFO) << "  insertion_hook= " << static_cast<int>(insertion_hook_);

  PrintHeader(static_cast<int64_t>(payload_.size()));

  LOG(INFO) << "  payload omitted.";
}

}  // namespace iamf_tools
