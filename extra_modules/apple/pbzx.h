// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_APPLE_PBZX_H_
#define XFF_APPLE_PBZX_H_
#include "absl/status/statusor.h"
#include "xff/vfs/read_source.h"

namespace xff::apple {
// Recognizes PBZX and returns a restartable decoder. InvalidArgument means a different format.
// Supports length-framed raw/XZ chunks, with a 64 MiB per-chunk and 128 MiB XZ memory bound.
absl::StatusOr<vfs::SharedReadSource> DecodePbzx(vfs::SharedReadSource source);
}  // namespace xff::apple
#endif  // XFF_APPLE_PBZX_H_
