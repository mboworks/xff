// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#ifndef XFF_EXTRAS_API_HOST_DESCRIPTORS_H_
#define XFF_EXTRAS_API_HOST_DESCRIPTORS_H_

#include <sys/types.h>

#include <string>

namespace xff::vfs::host {
// Private fixed-signature POSIX boundary. These preserve descriptor/errno semantics;
// callers own successful descriptors and translate failures into contextual statuses.
// Tests link a substitute implementation, with no runtime override in production.
int Open(const std::string& path, int flags, mode_t mode = 0);
int OpenAt(int parent, const std::string& path, int flags, mode_t mode = 0);
int Duplicate(int descriptor);
}  // namespace xff::vfs::host

#endif  // XFF_EXTRAS_API_HOST_DESCRIPTORS_H_
