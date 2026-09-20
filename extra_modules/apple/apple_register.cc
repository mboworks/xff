// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "absl/strings/match.h"
#include "xff/apple/pbzx.h"
#include "xff/archive/archive_extension.h"
#include "xff/archive/archive_register.h"
#include "xff/license/notice.h"

namespace xff::apple {
namespace {
// NOLINTNEXTLINE(fuchsia-statically-constructed-objects,cert-err58-cpp)
const struct AppleRegistrar {
  AppleRegistrar() {
    archive::RegisterContainerNameProbe("apple", [](std::string_view path) {
      const auto slash = path.rfind('/');
      if (slash == std::string_view::npos) {
        return false;
      }
      const auto name = path.substr(slash + 1);
      if (name != "Payload" && name != "Scripts" && name != "Content" && name != "Archive.pax.gz") {
        return false;
      }
      path = path.substr(0, slash);
      while (!path.empty()) {
        if (absl::EndsWithIgnoreCase(path, ".pkg") || absl::EndsWithIgnoreCase(path, ".mpkg")) {
          return true;
        }
        const auto parent = path.rfind('/');
        if (parent == std::string_view::npos) {
          break;
        }
        path = path.substr(0, parent);
      }
      return false;
    });
    archive::RegisterSourceDecoder("pbzx", &DecodePbzx);
    archive::RegisterCompressionExtension({
        .name = "pbzx",
        .suffixes = {".pbzx"},
        .read_formats = {{
            .name = "pbzx",
            .suffixes = {".pbzx"},
            .detail =
                "Apple PKG/XIP payloads (raw/XZ chunks), readable on Linux and macOS; no DMG or installer execution",
        }},
    });
    archive::RegisterArchiveBackend();
  }
} kRegisterApple;

const license::Registrar kNotice{{
    .section = "Apple packages (@xff_apple)",
    .section_lead = true,
    .component = "xff Apple package reader",
    .spdx = "Apache-2.0",
    .text = "Copyright M. Boerger, the MBO Works authors. Licensed under the Apache License, Version 2.0.\n\n"
            "Cross-platform PBZX package payload support using the archive extension's liblzma codec.",
}};
}  // namespace
}  // namespace xff::apple
