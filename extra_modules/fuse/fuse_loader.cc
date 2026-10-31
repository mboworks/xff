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

#include "xff/fuse/fuse_loader.h"

#include <dlfcn.h>

#include <array>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "absl/types/span.h"

namespace xff::fuse {
namespace {

// The fuse3 lowlevel surface of the read-only mount server: session lifecycle, the mount pair, the
// request accessors, and the reply calls for lookup/getattr/readdir/open/read. Extending the server
// extends THIS list, so "available" keeps meaning "every call the server makes will resolve".
constexpr std::array kRequiredSymbols = std::to_array<std::string_view>({
    "fuse_add_direntry",
    "fuse_opt_free_args",
    "fuse_reply_attr",
    "fuse_reply_buf",
    "fuse_reply_entry",
    "fuse_reply_err",
    "fuse_reply_open",
    "fuse_reply_readlink",
    "fuse_req_userdata",
    "fuse_session_destroy",
    "fuse_session_exit",
    "fuse_session_loop",
    "fuse_session_mount",
    "fuse_session_new",
    "fuse_session_unmount",
});

// Platform library candidates, most specific first. Only fuse3: a machine with just the legacy
// fuse2 library (older macFUSE) reports unavailable - see the header for why that is a decision.
constexpr std::array kLibraryCandidates = std::to_array<std::string_view>({
#if defined(__APPLE__)
    "/usr/local/lib/libfuse3.3.dylib",
    "/usr/local/lib/libfuse3.dylib",
    "libfuse3.dylib",
#else
    "libfuse3.so.4",
    "libfuse3.so.3",
    "libfuse3.so",
#endif
});

}  // namespace

absl::Span<const std::string_view> RequiredSymbols() {
  return kRequiredSymbols;
}

FuseProbeResult ProbeFuseLibraries(
    absl::Span<const std::string_view> candidates,
    absl::Span<const std::string_view> symbols,
    absl::FunctionRef<void*(std::string_view)> open_library,
    absl::FunctionRef<void*(void*, std::string_view)> find_symbol) {
  FuseProbeResult result;
  for (const std::string_view candidate : candidates) {
    void* handle = open_library(candidate);
    if (handle == nullptr) {
      continue;
    }
    for (const std::string_view symbol : symbols) {
      if (find_symbol(handle, symbol) == nullptr) {
        result.error = absl::StrCat(candidate, " is present but lacks ", symbol, "; not mountable");
        // Keep probing: a second candidate may be a complete installation.
        handle = nullptr;
        break;
      }
    }
    if (handle != nullptr) {
      result.handle = handle;
      result.library = std::string(candidate);
      result.error.clear();
      return result;
    }
  }
  if (result.error.empty()) {
    result.error = "no FUSE3 library on this machine (looked for libfuse3); mounting is unavailable";
  }
  return result;
}

FuseLoader::FuseLoader() {
  const FuseProbeResult result = ProbeFuseLibraries(
      kLibraryCandidates, kRequiredSymbols,
      [](std::string_view candidate) {
        // dlopen wants a C string; the candidates are string literals, the copy makes that explicit.
        return ::dlopen(std::string(candidate).c_str(), RTLD_NOW | RTLD_LOCAL);
      },
      [](void* handle, std::string_view symbol) { return ::dlsym(handle, std::string(symbol).c_str()); });
  handle_ = result.handle;
  library_ = result.library;
  error_ = result.error;
}

const FuseLoader& FuseLoader::Instance() {
  // A magic static: the probe runs once, single-threaded by construction, and the instance lives
  // for the process (see the destructor note in the header).
  static const FuseLoader* const kInstance = new FuseLoader();
  return *kInstance;
}

void* FuseLoader::Symbol(std::string_view name) const {
  // The unavailable state is covered through ProbeFuseLibraries; the process singleton reflects
  // only this host's one library state during a coverage run.
  if (handle_ == nullptr) {  // LCOV_EXCL_BR_LINE
    return nullptr;
  }
  return ::dlsym(handle_, std::string(name).c_str());
}

bool FuseAvailable() {
  return FuseLoader::Instance().available();
}

}  // namespace xff::fuse
