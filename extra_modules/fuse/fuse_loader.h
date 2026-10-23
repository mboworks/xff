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

#ifndef XFF_FUSE_FUSE_LOADER_H_
#define XFF_FUSE_FUSE_LOADER_H_

// The runtime gate of the FUSE extra (epic #183): whether THIS machine can mount at all.
//
// There is deliberately no build-time libfuse dependency. FUSE is a per-machine capability - on
// macOS it is a kernel component the user installs (macFUSE), on Linux a library that may or may
// not be present - so the library is dlopen'ed at first use and every fuse3 lowlevel symbol the
// mount server needs is resolved then. One binary runs everywhere; where the library is absent,
// `--archive-mount` degrades to extraction and this loader says why.
//
// Scope: the FUSE3 API, including Linux SONAMEs 3 and 4. The ABI subset in fuse_abi.h
// works with both; SONAME 4 is still the FUSE3 API. macFUSE installations that ship only the legacy fuse2 library
// report unavailable (with the library name in `error()`); supporting the fuse2 lowlevel API is a separate decision to
// take when a real macFUSE user appears, not a default.

#include <string>
#include <string_view>

#include "absl/functional/function_ref.h"
#include "absl/types/span.h"

namespace xff::fuse {

// The fuse3 lowlevel symbols the mount server (build-plan slice 3) calls. Resolved eagerly so that
// "available" MEANS mountable: a library that is present but incomplete reports unavailable rather
// than failing at first use mid-walk.
[[nodiscard]] absl::Span<const std::string_view> RequiredSymbols();

// Result of probing an ordered set of FUSE libraries. The callbacks keep the platform loader at
// the boundary and make every fallback/error outcome deterministic in tests.
struct FuseProbeResult {
  void* handle = nullptr;
  std::string library;
  std::string error;
};

[[nodiscard]] FuseProbeResult ProbeFuseLibraries(
    absl::Span<const std::string_view> candidates,
    absl::Span<const std::string_view> symbols,
    absl::FunctionRef<void*(std::string_view)> open_library,
    absl::FunctionRef<void*(void*, std::string_view)> find_symbol);

class FuseLoader {
 public:
  // The process-wide instance; the library probe runs once, on first call.
  static const FuseLoader& Instance();

  // Whether the platform FUSE library was found with every required symbol.
  [[nodiscard]] bool available() const { return handle_ != nullptr; }

  // The library name that was loaded, or empty when unavailable.
  [[nodiscard]] std::string_view library() const { return library_; }

  // Why the loader is unavailable, or empty when it is available.
  [[nodiscard]] std::string_view error() const { return error_; }

  // The resolved address of a required symbol, or nullptr (always nullptr when unavailable). The
  // mount server casts these to the fuse3 function types at its call sites.
  [[nodiscard]] void* Symbol(std::string_view name) const;

  FuseLoader(const FuseLoader&) = delete;
  FuseLoader& operator=(const FuseLoader&) = delete;
  FuseLoader(FuseLoader&&) = delete;
  FuseLoader& operator=(FuseLoader&&) = delete;

 private:
  FuseLoader();
  // Never dlclose'd: the process-wide instance lives for the process, and unloading a FUSE library
  // with a mount in flight would be worse than the leak-on-exit.
  ~FuseLoader() = default;

  void* handle_ = nullptr;
  std::string library_;
  std::string error_;
};

// Convenience for the common question; same probe as Instance().available().
[[nodiscard]] bool FuseAvailable();

}  // namespace xff::fuse

#endif  // XFF_FUSE_FUSE_LOADER_H_
