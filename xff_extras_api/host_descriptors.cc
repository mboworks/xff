// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/vfs/host_descriptors.h"

#include <fcntl.h>

namespace xff::vfs::host {
// XFF_HOST_IO: fixed-signature boundary for authorized descriptor opens.
int Open(const std::string& path, int flags, mode_t mode) {
  // The OS declaration is variadic; xff always supplies the explicit mode.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  return ::open(path.c_str(), flags, mode);
}

// XFF_HOST_IO: fixed-signature boundary for authorized anchored descriptor opens.
int OpenAt(int parent, const std::string& path, int flags, mode_t mode) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  return ::openat(parent, path.c_str(), flags, mode);
}

// XFF_HOST_IO: retains an already authorized descriptor with close-on-exec.
int Duplicate(int descriptor) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  return ::fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
}
}  // namespace xff::vfs::host
