// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ARCHIVE_ARCHIVE_TAR_PATHNAME_TEST_SUPPORT_H_
#define XFF_ARCHIVE_ARCHIVE_TAR_PATHNAME_TEST_SUPPORT_H_

#include <cstddef>
#include <string>
#include <string_view>

namespace xff::archive::archive_tar_pathname_test_support {

// A zero-length USTAR member followed by two end blocks. Construct the header directly:
// a writer may normalize the deliberately inconsistent regular-file/trailing-slash pair.
inline std::string MakeTar(std::string_view name, char type = '0') {
  std::string bytes(std::size_t{3} * 512, '\0');
  bytes.replace(0, name.size(), name);
  bytes.replace(100, 7, "0000644");
  bytes.replace(108, 7, "0000000");
  bytes.replace(116, 7, "0000000");
  bytes.replace(124, 11, "00000000000");
  bytes.replace(136, 11, "00000000000");
  bytes.replace(148, 8, 8, ' ');
  bytes[156] = type;
  bytes.replace(257, 5, "ustar");
  bytes.replace(263, 2, "00");
  unsigned checksum = 0;
  for (std::size_t index = 0; index < 512; ++index) {
    checksum += static_cast<unsigned char>(bytes[index]);
  }
  for (std::size_t index = 154; index > 148;) {
    bytes[--index] = static_cast<char>('0' + (checksum & 7U));
    checksum >>= 3U;
  }
  bytes[154] = '\0';
  bytes[155] = ' ';
  return bytes;
}

}  // namespace xff::archive::archive_tar_pathname_test_support

#endif  // XFF_ARCHIVE_ARCHIVE_TAR_PATHNAME_TEST_SUPPORT_H_
