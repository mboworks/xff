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

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "xff/presentation/fields/fields.h"
#include "xff/vfs/entry.h"

namespace xff::fields {
namespace {

using ::mbo::testing::EqualsText;
namespace stdfs = ::std::filesystem;

struct LexicalFieldsTest : ::testing::Test {};

TEST_F(LexicalFieldsTest, BorrowedComponentsMatchNativeLexicalPaths) {
  std::vector<std::string> paths{
      "",
      "/",
      "//",
      "///",
      ".",
      "..",
      "...",
      ".hidden",
      ".hidden.cc",
      "file.",
      "file..",
      "dir/",
      "dir//",
      "dir/.",
      "dir/..",
      "dir///file.tar.gz",
      "//dir/file",
      "/dir/file/",
      "archive.tar!member/.file.cc",
      "a\\b.cc",
      std::string("dir/file\0.cc", 12),
      std::string(200, 'd') + "/" + std::string(200, 'f') + ".tar.gz",
  };
  static constexpr auto kParts = std::to_array<std::string_view>({"", ".", "..", "a", ".a", "a.", "a.b.c", "a..b"});
  static constexpr auto kSeparators = std::to_array<std::string_view>({"/", "//", "///"});
  for (const std::string_view left : kParts) {
    for (const std::string_view separator : kSeparators) {
      for (const std::string_view right : kParts) {
        paths.push_back(std::string(left) + std::string(separator) + std::string(right));
      }
    }
  }
  static constexpr auto kFields =
      std::to_array<std::string_view>({"name", "stem", "ext", "suffix", "suffixes", "core", "dir"});
  std::array<Template, kFields.size()> direct;
  std::array<Template, kFields.size()> qualified;
  for (std::size_t at = 0; at < kFields.size(); ++at) {
    const std::string field(kFields.at(at));
    direct.at(at) = Template::Compile("{" + field + "}");
    qualified.at(at) = Template::Compile("{path:" + field + "}");
  }
  const vfs::Metadata metadata;
  for (const std::string& path : paths) {
    SCOPED_TRACE(path);
    const stdfs::path native(path);
    const std::string filename = native.filename().string();
    const std::string suffix = native.extension().string();
    const std::size_t dot = filename.find('.', 1);
    const std::array<std::string, kFields.size()> expected{
        filename,
        native.stem().string(),
        suffix.empty() ? suffix : suffix.substr(1),
        suffix,
        dot == std::string::npos ? "" : filename.substr(dot),
        dot == std::string::npos ? filename : filename.substr(0, dot),
        native.parent_path().empty() ? "." : native.parent_path().string(),
    };
    // A nonterminated slice catches accidental reliance on the input's c_str boundary.
    const std::string storage = "before" + path + "after";
    const RenderContext context{.path = std::string_view(storage).substr(6, path.size()), .metadata = metadata};
    for (std::size_t at = 0; at < kFields.size(); ++at) {
      SCOPED_TRACE(kFields.at(at));
      EXPECT_THAT(direct.at(at).Render(context), EqualsText(expected.at(at)));
      EXPECT_THAT(qualified.at(at).Render(context), EqualsText(expected.at(at)));
    }
  }
}

TEST_F(LexicalFieldsTest, MultipleFieldsAndOwnedTransformInputsRetainTheirLifetimes) {
  const Template compiled =
      Template::Compile("{name}|{stem}|{ext}|{suffix}|{suffixes}|{core}|{path:stem}|{path:s|^|prefix/|;s|$|.txt|}");
  const vfs::Metadata metadata;
  const RenderContext context{.path = "dir/archive.tar.gz", .metadata = metadata};
  EXPECT_THAT(
      compiled.Render(context),
      EqualsText("archive.tar.gz|archive.tar|gz|.gz|.tar.gz|archive|archive.tar|prefix/dir/archive.tar.gz.txt"));
  const Template rewritten = Template::Compile("{def.input:name}|{def.input:stem}|{def.input:core}");
  const std::map<std::string, std::string> definitions{{"input", "a/temporary.long.name"}};
  RenderContext defined = context;
  defined.defines.set_ref(definitions);
  EXPECT_THAT(rewritten.Render(defined), EqualsText("temporary.long.name|temporary.long|temporary"));
  // DirField owns its normalized parent. A component view must be consumed before that owner dies.
  const std::string parent = std::string(200, 'd') + ".tar.gz";
  const std::string path = "root/" + parent + "/file.txt";
  const RenderContext long_parent{.path = path, .metadata = metadata};
  const Template parent_components = Template::Compile("{dir:name}|{dir:stem}");
  EXPECT_THAT(parent_components.Render(long_parent), EqualsText(parent + "|" + std::string(200, 'd') + ".tar"));
}

}  // namespace
}  // namespace xff::fields
