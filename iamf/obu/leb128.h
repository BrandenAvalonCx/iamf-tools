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
#ifndef OBU_LEB128_H_
#define OBU_LEB128_H_

#include <cstdint>

namespace iamf_tools {

/*\!brief IAMF spec requires a ULEB128 or a SLEB128 be encoded in <= 8 bytes.
 */
inline constexpr int kMaxLeb128Size = 8;

/*!\brief A decoded `leb128` in IAMF.  */
typedef uint32_t DecodedUleb128;

/*!\brief A decoded `sleb128` in IAMF.  */
typedef int32_t DecodedSleb128;

}  // namespace iamf_tools

#endif  // OBU_LEB128_H_
