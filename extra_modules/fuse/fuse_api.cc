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

#include "xff/fuse/fuse_api.h"

#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "xff/fuse/fuse_loader.h"

namespace xff::fuse {
namespace {

// The single dlsym boundary: a void* from the loader becomes the declared function type. dlsym's
// contract IS this cast (POSIX defines it for function pointers); it is funneled here with the
// loader's eager resolution guaranteeing the symbol exists.
template<typename Fn>
absl::Status Assign(const FuseLoader& loader, std::string_view name, Fn& target) {
  void* symbol = loader.Symbol(name);
  if (symbol == nullptr) {  // LCOV_EXCL_BR_LINE: eager probing makes disappearance a fatal invariant.
    return absl::InternalError(absl::StrCat("symbol vanished between probe and use: ", name));
  }
  // NOLINTNEXTLINE(bugprone-casting-through-void,cppcoreguidelines-pro-type-reinterpret-cast)
  target = reinterpret_cast<Fn>(symbol);
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<FuseApi> FuseApi::Resolve() {
  const FuseLoader& loader = FuseLoader::Instance();
  if (!loader.Available()) {
    return absl::UnavailableError(loader.Error());
  }
  // Mutated through the reference parameter of Assign below, which misc-const-correctness cannot
  // see through the template - the NOLINT documents the false positive rather than restructuring
  // the one honest builder this struct has.
  FuseApi api;  // NOLINT(misc-const-correctness)
  absl::Status status = Assign(loader, "fuse_add_direntry", api.add_direntry);
  status.Update(Assign(loader, "fuse_session_new", api.session_new));
  status.Update(Assign(loader, "fuse_session_mount", api.session_mount));
  status.Update(Assign(loader, "fuse_session_loop", api.session_loop));
  status.Update(Assign(loader, "fuse_session_exit", api.session_exit));
  status.Update(Assign(loader, "fuse_session_unmount", api.session_unmount));
  status.Update(Assign(loader, "fuse_session_destroy", api.session_destroy));
  status.Update(Assign(loader, "fuse_reply_err", api.reply_err));
  status.Update(Assign(loader, "fuse_reply_attr", api.reply_attr));
  status.Update(Assign(loader, "fuse_reply_entry", api.reply_entry));
  status.Update(Assign(loader, "fuse_reply_buf", api.reply_buf));
  status.Update(Assign(loader, "fuse_opt_free_args", api.opt_free_args));
  status.Update(Assign(loader, "fuse_reply_open", api.reply_open));
  status.Update(Assign(loader, "fuse_reply_readlink", api.reply_readlink));
  status.Update(Assign(loader, "fuse_req_userdata", api.req_userdata));
  if (!status.ok()) {  // LCOV_EXCL_BR_LINE: only reachable if an eagerly resolved symbol vanishes.
    return status;
  }
  return api;
}

}  // namespace xff::fuse
