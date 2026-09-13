// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef XFF_ENGINE_EXTRACT_H_
#define XFF_ENGINE_EXTRACT_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/mutations.h"

namespace xff::engine {

// Picks the directory an extracted member should be written to, preferring a MEMORY-backed one.
//
// The point of the preference: on Linux `$XDG_RUNTIME_DIR` and `/dev/shm` are tmpfs, so a member
// written there never reaches a disk, while the child process still gets an ordinary path - which is
// the whole reason extraction exists. It is a directory choice, not a mechanism: nothing else about
// extraction changes, and a platform without a tmpfs (macOS) simply falls through to `$TMPDIR`.
//
// A tmpfs is RAM, though, and a shared, capped one: writing a huge member there can fail or squeeze
// everything else on the machine. So a candidate is only taken when the member comfortably fits in the
// space it reports free - otherwise the next candidate is tried, and the last one (the ordinary
// temporary directory) is used whether it fits or not, because by then there is nowhere else to go.
//
// `candidates` is injectable so a test can describe a filesystem layout instead of depending on the
// machine it runs on; the default order is
// `$XDG_RUNTIME_DIR` (user-private tmpfs), `/dev/shm`, `std::filesystem::temp_directory_path()`.
[[nodiscard]] std::vector<std::string> DefaultExtractDirectories();

[[nodiscard]] std::string ChooseExtractDirectory(std::uint64_t member_size, absl::Span<const std::string> candidates);

// Gives an archive member a real path for the length of one run (`--archive-extract`).
//
// A member is bytes inside a container: there is no path a child process can open, which is why
// -exec on one is refused by default. Extracting it to a temporary file is the way to run a tool
// over it anyway, and the temporary file is exactly as real as any other - so the child needs no
// knowledge of archives, and a member reads as the file it would be if unpacked.
//
// Each member gets its OWN directory (`<tmp>/xff-<pid>-<n>/`) holding a file with the member's own
// basename: two members named `README` in different containers cannot collide, and a tool that
// keys on the name or the extension sees what it expects. Everything created is removed by
// `Release` or, for anything still held (a `+` batch runs after the walk), by the destructor - so
// nothing survives the run even when the child fails or a signal ends the walk early.
//
// Not thread-safe: the evaluator is single-threaded, and a `-j` child is launched from that same
// thread.
class ExtractedMembers {
 public:
  explicit ExtractedMembers(vfs::MutationPolicy policy = {}) : policy_(policy) {}

  ~ExtractedMembers();

  ExtractedMembers(const ExtractedMembers&) = delete;
  ExtractedMembers& operator=(const ExtractedMembers&) = delete;
  ExtractedMembers(ExtractedMembers&&) = delete;
  ExtractedMembers& operator=(ExtractedMembers&&) = delete;

  // Reads `member` through `fs` (the container's filesystem, not the local one) and writes it to a
  // fresh temporary file, returning that file's path. The bytes are the member's real, decompressed
  // content, so the temporary file's size is the size xff reports for the member.
  absl::StatusOr<std::string> Extract(const vfs::FileSystem& fs, std::string_view member);

  // Removes one extracted file and its directory. A path this instance did not hand out is ignored,
  // so a caller may release unconditionally.
  void Release(std::string_view path);

  // Extracted files not yet released, for tests and for the end-of-run sweep.
  [[nodiscard]] std::vector<std::string> Held() const;

 private:
  std::vector<std::string> held_;  // full paths, each the only file in its own directory
  vfs::MutationPolicy policy_;
  std::map<std::string, std::unique_ptr<vfs::TemporaryDirectory>> directories_;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_EXTRACT_H_
