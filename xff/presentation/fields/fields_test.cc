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

#include "xff/presentation/fields/fields.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/env/env.h"
#include "xff/vfs/entry.h"

namespace xff::fields {
namespace {

using ::mbo::StringOrView;
using ::testing::_;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::Optional;
using ::testing::UnorderedElementsAreArray;

struct FieldsTest : ::testing::Test {
  static vfs::Metadata Meta(vfs::FileType type, std::uint64_t size) {
    vfs::Metadata md;
    md.type = type;
    md.size = size;
    return md;
  }
};

TEST_F(FieldsTest, StringOrViewOwnsComputedStringsAndPreservesStableViews) {
  const StringOrView owned(std::string("computed"));
  EXPECT_THAT(owned.view(), Eq("computed"));
  EXPECT_THAT(owned.owns_string(), IsTrue());

  const std::string stable = "stable";
  const StringOrView viewed{std::string_view(stable)};
  EXPECT_THAT(viewed.view(), Eq("stable"));
  EXPECT_THAT(viewed.owns_string(), IsFalse());
  EXPECT_THAT(viewed.view().data(), Eq(stable.data()));
}

TEST_F(FieldsTest, SubstitutesPathComponentsAndMetadata) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 12);
  EXPECT_THAT(
      Render("{name} in {dir} [{ext}] {size}b {type}", "a/b/file.tar.gz", md, 2), "file.tar.gz in a/b [gz] 12b f");
  EXPECT_THAT(Render("{stem}", "a/b/file.tar.gz", md, 2), "file.tar");
}

TEST_F(FieldsTest, MimeFieldRendersMediaTypeByExtension) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  EXPECT_THAT(Render("{mime}", "a/b/notes.txt", md, 0), "text/plain");
  EXPECT_THAT(Render("{mime}", "a/b/readme.md", md, 0), "text/markdown");
  EXPECT_THAT(Render("{mime}", "a/b/README", md, 0), "application/octet-stream");  // no extension
}

TEST_F(FieldsTest, UnknownLanguageMetadataFieldsRenderEmpty) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  EXPECT_THAT(Render("[{lang-type}][{lang-color}][{lang-group}][{lang-source}]", "a/b/photo.jpg", md, 0), "[][][][]");
}

TEST_F(FieldsTest, OwnerIsAnAliasOfUser) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  // {owner} is an alias of {user}: both render the same owner name (the value is runtime-dependent).
  EXPECT_THAT(Render("{owner}", "f", md, 0), Render("{user}", "f", md, 0));
}

TEST_F(FieldsTest, BlocksFieldRendersAllocatedSpace) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 1);         // 1 apparent byte
  md.blocks = 16;                                              // 16 * 512 = 8 KiB allocated
  EXPECT_THAT(Render("{blocks}", "f", md, 0), "16");           // allocated 512-blocks (find's %b)
  EXPECT_THAT(Render("{blocks:h}", "f", md, 0), "8.0K");       // human-readable allocated bytes
  EXPECT_THAT(Render("{size} {blocks}", "f", md, 0), "1 16");  // apparent vs allocated differ
}

TEST_F(FieldsTest, DirOfTopLevelEntryIsDot) {
  EXPECT_THAT(Render("{dir}", "file", Meta(vfs::FileType::kRegular, 0), 0), ".");
}

TEST_F(FieldsTest, RelpathIsThePathRelativeToTheSearchRoot) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const Template compiled = Template::Compile("{relpath}");
  // Descendant: the root prefix and its separator are stripped (find's %P).
  EXPECT_THAT(compiled.Render(RenderContext{.path = "A/sub/x.txt", .root = "A", .metadata = md}), "sub/x.txt");
  // The root operand itself renders empty.
  EXPECT_THAT(compiled.Render(RenderContext{.path = "A", .root = "A", .metadata = md}), "");
  // No root recorded: best-effort whole path.
  EXPECT_THAT(compiled.Render(RenderContext{.path = "A/x", .metadata = md}), "A/x");
  EXPECT_THAT(compiled.Render(RenderContext{.path = "A///x", .root = "A", .metadata = md}), "x");
  EXPECT_THAT(compiled.Render(RenderContext{.path = "elsewhere/x", .root = "A", .metadata = md}), "elsewhere/x");
}

TEST_F(FieldsTest, DoubledBracesAreLiteral) {
  EXPECT_THAT(Render("{{x}}={name}", "p/q", Meta(vfs::FileType::kRegular, 0), 0), "{x}=q");
}

TEST_F(FieldsTest, UnknownFieldRendersEmpty) {
  EXPECT_THAT(Render("[{bogus}]", "p", Meta(vfs::FileType::kRegular, 0), 0), "[]");
}

TEST_F(FieldsTest, LineAndTextRenderTheGrepMatchLine) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const Template compiled = Template::Compile("{path}:{line}:{text}");
  EXPECT_THAT(
      compiled.Render(RenderContext{.path = "src/a.py", .metadata = md, .line_number = 12, .line_text = "  # TODO"}),
      "src/a.py:12:  # TODO");
}

TEST_F(FieldsTest, LineAndTextAreEmptyWithoutAMatchLine) {
  // line_number unset (outside a -grep line): {line}/{text} render empty so they
  // no-op in --template / -printf.
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const Template compiled = Template::Compile("[{line}][{text}]");
  EXPECT_THAT(compiled.Render(RenderContext{.path = "f", .metadata = md}), "[][]");
}

TEST_F(FieldsTest, MatchAndColumnRenderTheMatchedSpan) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const Template compiled = Template::Compile("{column}:{match}");
  EXPECT_THAT(
      compiled.Render(RenderContext{.path = "f", .metadata = md, .match_text = "E42", .match_column = 6}), "6:E42");
}

TEST_F(FieldsTest, MatchAndColumnAreEmptyWithoutASpan) {
  // match_column unset: {match}/{column} render empty (they only fire for -grep -o).
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const Template compiled = Template::Compile("[{column}][{match}]");
  EXPECT_THAT(compiled.Render(RenderContext{.path = "f", .metadata = md}), "[][]");
}

TEST_F(FieldsTest, HashFieldDigestsFileContent) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 3);
  const std::string path = std::string(::testing::TempDir()) + "/xff_fields_hash_abc";
  { std::ofstream(path) << "abc"; }
  // Bare {hash} is sha256 hex; the qualifier picks the algorithm and (after /) the encoding.
  EXPECT_THAT(
      Template::Compile("{hash}").Render(RenderContext{.path = path, .metadata = md}),
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_THAT(
      Template::Compile("{hash:md5}").Render(RenderContext{.path = path, .metadata = md}),
      "900150983cd24fb0d6963f7d28e17f72");
  EXPECT_THAT(
      Template::Compile("{hash:sha256/base64}").Render(RenderContext{.path = path, .metadata = md}),
      "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=");
  // An unknown algorithm or encoding renders empty (the field convention).
  EXPECT_THAT(Template::Compile("[{hash:crc32}]").Render(RenderContext{.path = path, .metadata = md}), "[]");
  EXPECT_THAT(Template::Compile("[{hash:sha256/b64}]").Render(RenderContext{.path = path, .metadata = md}), "[]");
  (void)std::remove(path.c_str());
}

TEST_F(FieldsTest, HashFieldOfUnreadableFileIsEmpty) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::string absent = std::string(::testing::TempDir()) + "/xff_fields_hash_absent";
  EXPECT_THAT(Template::Compile("[{hash}]").Render(RenderContext{.path = absent, .metadata = md}), "[]");
}

TEST_F(FieldsTest, LinesFieldCountsTextLines) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::string path = std::string(::testing::TempDir()) + "/xff_fields_lines";
  { std::ofstream(path) << "one\ntwo\nthree\n"; }
  EXPECT_THAT(Template::Compile("{lines}").Render(RenderContext{.path = path, .metadata = md}), "3");
  { std::ofstream(path) << "no trailing newline"; }  // a final unterminated line still counts
  EXPECT_THAT(Template::Compile("{lines}").Render(RenderContext{.path = path, .metadata = md}), "1");
  { std::ofstream(path) << ""; }  // truncate: an empty file is zero lines
  EXPECT_THAT(Template::Compile("{lines}").Render(RenderContext{.path = path, .metadata = md}), "0");
  (void)std::remove(path.c_str());
}

TEST_F(FieldsTest, LinesFieldIsEmptyForBinaryUnreadableOrNonRegular) {
  const vfs::Metadata reg = Meta(vfs::FileType::kRegular, 0);
  const std::string path = std::string(::testing::TempDir()) + "/xff_fields_lines_bin";
  { std::ofstream(path, std::ios::binary).write("a\0b\n", 4); }  // a NUL byte in the content
  // A NUL byte marks the file binary -> empty, like the content detector.
  EXPECT_THAT(Template::Compile("[{lines}]").Render(RenderContext{.path = path, .metadata = reg}), "[]");
  // A regular file that cannot be read -> empty.
  const std::string absent = std::string(::testing::TempDir()) + "/xff_fields_lines_absent";
  EXPECT_THAT(Template::Compile("[{lines}]").Render(RenderContext{.path = absent, .metadata = reg}), "[]");
  // A non-regular entry is never counted, even at a readable path.
  const vfs::Metadata dir = Meta(vfs::FileType::kDirectory, 0);
  EXPECT_THAT(Template::Compile("[{lines}]").Render(RenderContext{.path = path, .metadata = dir}), "[]");
  (void)std::remove(path.c_str());
}

TEST_F(FieldsTest, TimeFieldQualifiers) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mtime = absl::FromUnixSeconds(1'700'000'000);  // 2023-11-14, mid-month: the year is timezone-stable
  EXPECT_THAT(Render("{mtime:epoch}", "f", md, 0), "1700000000");
  EXPECT_THAT(Render("{mtime:%Y}", "f", md, 0), "2023");
  EXPECT_THAT(Render("{mtime:iso}", "f", md, 0), HasSubstr("2023"));
  EXPECT_THAT(Render("{mtime}", "f", md, 0), HasSubstr("2023"));  // default ISO-8601
}

TEST_F(FieldsTest, EveryMetadataTimeAndIdentityFieldIsRendered) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.atime = absl::FromUnixSeconds(1);
  md.btime = absl::FromUnixSeconds(2);
  md.ctime = absl::FromUnixSeconds(3);
  md.ino = 42;
  md.nlink = 7;
  const Template compiled = Template::Compile("{atime:epoch}|{btime:epoch}|{ctime:epoch}|{depth}|{inode}|{links}");
  EXPECT_THAT(
      compiled.Render(RenderContext{.path = "f", .metadata = md, .depth = 5, .tz = absl::UTCTimeZone()}),
      "1|2|3|5|42|7");
}

TEST_F(FieldsTest, MissingBirthTimeRendersEmptyWhileOtherTimesRemainAvailable) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.atime = absl::FromUnixSeconds(1);
  md.ctime = absl::FromUnixSeconds(3);
  EXPECT_THAT(Render("[{btime:epoch}]|{atime:epoch}|{ctime:epoch}", "f", md, 0), "[]|1|3");
}

TEST_F(FieldsTest, TimeFieldsRenderInTheContextZone) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mtime = absl::FromUnixSeconds(1'600'000'000);  // 2020-09-13 12:26:40 UTC
  const Template compiled = Template::Compile("{mtime:%z}|{mtime:%H}");
  // UTC: zero offset, hour 12 (RenderContext::tz drives the formatting zone).
  EXPECT_THAT(
      compiled.Render(RenderContext{.path = "f", .metadata = md, .depth = 0, .tz = absl::UTCTimeZone()}), "+0000|12");
  // UTC+1 (fixed): +0100 offset, hour 13 -- the same instant, a different zone.
  EXPECT_THAT(
      compiled.Render(RenderContext{.path = "f", .metadata = md, .depth = 0, .tz = absl::FixedTimeZone(3'600)}),
      "+0100|13");
}

TEST_F(FieldsTest, BareTimeFieldUsesTheTimeFormatDefault) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mtime = absl::FromUnixSeconds(1'600'000'000);  // 2020-09-13 12:26:40 UTC
  const Template bare = Template::Compile("{mtime}");
  // A bare {mtime} (no qualifier) renders with the --time-format default...
  EXPECT_THAT(
      bare.Render(RenderContext{.path = "f", .metadata = md, .tz = absl::UTCTimeZone(), .time_format = "epoch"}),
      "1600000000");
  // ...with no --time-format default it falls back to the "space" form...
  EXPECT_THAT(
      bare.Render(RenderContext{.path = "f", .metadata = md, .tz = absl::UTCTimeZone()}), "2020-09-13 12:26:40 +0000");
  // ...and an explicit {mtime:QUAL} qualifier always wins over the default.
  const Template qualified = Template::Compile("{mtime:%Y}");
  EXPECT_THAT(
      qualified.Render(RenderContext{.path = "f", .metadata = md, .tz = absl::UTCTimeZone(), .time_format = "epoch"}),
      "2020");
}

TEST_F(FieldsTest, ModeAndOwnerFields) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mode = 0644;
  md.uid = 1'234'567;  // unlikely to have a passwd/group entry -> numeric fallback (find's %u/%g)
  md.gid = 1'234'567;
  md.dev = 42;
  EXPECT_THAT(Render("{mode} {user}:{group}", "f", md, 0), "644 1234567:1234567");
  EXPECT_THAT(Render("{uid}:{gid} dev={dev}", "f", md, 0), "1234567:1234567 dev=42");  // numeric ids + device
}

TEST_F(FieldsTest, AccessRendersTheSymbolicModeString) {
  vfs::Metadata reg = Meta(vfs::FileType::kRegular, 0);
  reg.mode = 0644;
  EXPECT_THAT(Render("{access}", "f", reg, 0), "-rw-r--r--");  // ls -l / stat %A style
  vfs::Metadata dir = Meta(vfs::FileType::kDirectory, 0);
  dir.mode = 0755;
  EXPECT_THAT(Render("{access}", "d", dir, 0), "drwxr-xr-x");
  vfs::Metadata suid = Meta(vfs::FileType::kRegular, 0);
  suid.mode = 04755;  // setuid + rwxr-xr-x -> owner exec shows 's'
  EXPECT_THAT(Render("{access}", "f", suid, 0), "-rwsr-xr-x");
  vfs::Metadata sticky = Meta(vfs::FileType::kDirectory, 0);
  sticky.mode = 01777;  // sticky + rwxrwxrwx -> other exec shows 't' (e.g. /tmp)
  EXPECT_THAT(Render("{access}", "d", sticky, 0), "drwxrwxrwt");
}

TEST_F(FieldsTest, TypeAndAccessCoverEveryFileKindAndSpecialPermissionState) {
  struct Case {
    vfs::FileType type;
    std::uint32_t mode;
  };

  constexpr std::array<Case, 8> kCases = {{
      {.type = vfs::FileType::kBlockDevice, .mode = 0000},
      {.type = vfs::FileType::kCharDevice, .mode = 0777},
      {.type = vfs::FileType::kDirectory, .mode = 07000},
      {.type = vfs::FileType::kFifo, .mode = 04100},
      {.type = vfs::FileType::kRegular, .mode = 02011},
      {.type = vfs::FileType::kSocket, .mode = 01001},
      {.type = vfs::FileType::kSymlink, .mode = 0644},
      {.type = vfs::FileType::kUnknown, .mode = 0000},
  }};
  std::vector<std::string> access;
  access.reserve(kCases.size());
  for (const Case& test : kCases) {
    vfs::Metadata metadata = Meta(test.type, 0);
    metadata.mode = test.mode;
    access.push_back(Render("{access}", "f", metadata, 0));
  }
  EXPECT_THAT(
      access, ElementsAre(
                  "b---------", "crwxrwxrwx", "d--S--S--T", "p--s------", "------s--x", "s--------t", "lrw-r--r--",
                  "?---------"));
  EXPECT_THAT(Render("{type}", "f", Meta(vfs::FileType::kUnknown, 0), 0), "U");
  EXPECT_THAT(Render("{mode}", "f", Meta(vfs::FileType::kRegular, 0), 0), "0");
}

TEST_F(FieldsTest, HumanSizeAndSuffixes) {
  EXPECT_THAT(Render("{size:h}", "f", Meta(vfs::FileType::kRegular, 1'536), 0), "1.5K");
  EXPECT_THAT(Render("{size:h}", "f", Meta(vfs::FileType::kRegular, 500), 0), "500");   // < 1 KiB: plain
  EXPECT_THAT(Render("{size}", "f", Meta(vfs::FileType::kRegular, 1'536), 0), "1536");  // no qualifier
  EXPECT_THAT(Render("{suffixes}", "a/b/file.tar.gz", Meta(vfs::FileType::kRegular, 0), 0), ".tar.gz");
  EXPECT_THAT(Render("{suffixes}", "a/b/file", Meta(vfs::FileType::kRegular, 0), 0), "");
  // {suffix} is the last extension WITH its dot; {ext} drops the dot; {suffixes} is all.
  EXPECT_THAT(Render("{suffix}|{ext}", "a/b/file.tar.gz", Meta(vfs::FileType::kRegular, 0), 0), ".gz|gz");
  EXPECT_THAT(Render("[{suffix}]", "a/b/file", Meta(vfs::FileType::kRegular, 0), 0), "[]");
}

TEST_F(FieldsTest, HumanSizeSelectsEverySupportedBinaryUnit) {
  constexpr std::uint64_t kKib = 1'024;
  constexpr std::uint64_t kMib = kKib * 1'024;
  constexpr std::uint64_t kGib = kMib * 1'024;
  constexpr std::uint64_t kTib = kGib * 1'024;
  constexpr std::uint64_t kPib = kTib * 1'024;
  constexpr std::uint64_t kEib = kPib * 1'024;
  EXPECT_THAT(
      Render(
          "{size:h}|{blocks:h}", "f",
          vfs::Metadata{.type = vfs::FileType::kRegular, .size = 2 * kEib, .blocks = 3 * kPib / 512}, 0),
      "2.0E|3.0P");
  EXPECT_THAT(Render("{size:h}", "f", Meta(vfs::FileType::kRegular, 4 * kTib), 0), "4.0T");
  EXPECT_THAT(Render("{size:h}", "f", Meta(vfs::FileType::kRegular, 5 * kGib), 0), "5.0G");
  EXPECT_THAT(Render("{size:h}", "f", Meta(vfs::FileType::kRegular, 6 * kMib), 0), "6.0M");
}

TEST_F(FieldsTest, CoreStripsAllExtensions) {
  // {core} = the filename with ALL extensions removed; complement of {suffixes}.
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  EXPECT_THAT(Render("{core}", "a/b/foo.tar.gz", md, 0), "foo");                     // vs {stem}="foo.tar"
  EXPECT_THAT(Render("{core}|{suffixes}", "a/b/foo.tar.gz", md, 0), "foo|.tar.gz");  // core + suffixes == name
  EXPECT_THAT(Render("{core}", "a/b/foo", md, 0), "foo");                            // no extension
  EXPECT_THAT(Render("{core}", "a/b/.bashrc", md, 0), ".bashrc");  // a leading dot is not an extension
}

TEST_F(FieldsTest, PathComponentQualifierDecomposesAnyPathValuedField) {
  // A component qualifier post-processes, treating the field's value as a path -- so
  // {path:X} mirrors the flat {X} field, and it composes onto {relpath}, {def.NAME}, etc.
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::map<std::string, std::string> defs = {{"B", "/other/tree/report.tar.gz"}};
  const Template compiled = Template::Compile("{path:name}|{path:stem}|{path:core}|{path:ext}|{path:dir}");
  EXPECT_THAT(
      compiled.Render(RenderContext{.path = "a/b/foo.tar.gz", .metadata = md}), "foo.tar.gz|foo.tar|foo|gz|a/b");
  // {path:name} equals the flat {name}.
  EXPECT_THAT(Render("{path:name}", "a/b/foo.tar.gz", md, 0), Render("{name}", "a/b/foo.tar.gz", md, 0));
  // Composes on {relpath} (root-relative) and on a {def.NAME} value.
  EXPECT_THAT(
      Template::Compile("{relpath:dir}").Render(RenderContext{.path = "A/sub/x.cc", .root = "A", .metadata = md}),
      "sub");
  EXPECT_THAT(
      Template::Compile("{def.B:core}").Render(RenderContext{.path = "f", .metadata = md, .defines = defs}), "report");
}

TEST_F(FieldsTest, TargetRendersTheSymlinkTargetAndComposes) {
  const vfs::Metadata link = Meta(vfs::FileType::kSymlink, 0);
  const Template compiled = Template::Compile("{target}|{target:name}|{target:core}");
  EXPECT_THAT(
      compiled.Render(RenderContext{.path = "l", .link_target = "sub/report.tar.gz", .metadata = link}),
      "sub/report.tar.gz|report.tar.gz|report");
  // Empty for a non-symlink (link_target unset by the driver).
  EXPECT_THAT(Render("[{target}]", "f", Meta(vfs::FileType::kRegular, 0), 0), "[]");
}

TEST_F(FieldsTest, QuotedQualifierCarriesBracesColonsAndQuotes) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mtime = absl::FromUnixSeconds(1'700'000'000);  // 2023-11-14, mid-month: the year is timezone-stable
  // Escaped quotes/braces survive into the strftime format; the inner '}' does not end the field.
  EXPECT_THAT(Render(R"({mtime:"{\"y\":\"%Y\"}"})", "f", md, 0), R"({"y":"2023"})");
  // A quoted qualifier is dequoted before matching, so {size:"h"} still selects human-readable size.
  EXPECT_THAT(Render(R"({size:"h"})", "f", Meta(vfs::FileType::kRegular, 1'536), 0), "1.5K");
  // An unterminated quoted qualifier leaves the '{' and the remaining text literal.
  EXPECT_THAT(Render(R"({mtime:"%Y)", "f", md, 0), R"({mtime:"%Y)");
}

TEST_F(FieldsTest, CompiledTemplateRendersManyEntries) {
  const Template compiled = Template::Compile("{name}={size:h}");  // parsed once, reused below
  const vfs::Metadata small = Meta(vfs::FileType::kRegular, 1);
  const vfs::Metadata big = Meta(vfs::FileType::kRegular, 1'536);
  EXPECT_THAT(compiled.Render(RenderContext{.path = "a/x", .metadata = small, .depth = 0}), "x=1");
  EXPECT_THAT(compiled.Render(RenderContext{.path = "b/big", .metadata = big, .depth = 0}), "big=1.5K");
}

TEST_F(FieldsTest, FieldNameAliasesResolveToTheSameRenderer) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mode = 0700;
  EXPECT_THAT(Render("{file}|{name}", "a/b.txt", md, 0), "b.txt|b.txt");
  EXPECT_THAT(Render("{extension}|{ext}", "a/b.txt", md, 0), "txt|txt");
  EXPECT_THAT(Render("{perm}|{mode}", "a/b.txt", md, 0), "700|700");
}

TEST_F(FieldsTest, RootFieldReportsTheSearchRoot) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const Template compiled = Template::Compile("{root}|{path}");
  EXPECT_THAT(compiled.Render(RenderContext{.path = "r/sub/f", .root = "r", .metadata = md, .depth = 2}), "r|r/sub/f");
  // An empty root (e.g. via the rootless convenience overload) renders empty.
  EXPECT_THAT(compiled.Render(RenderContext{.path = "x", .metadata = md, .depth = 0}), "|x");
}

TEST_F(FieldsTest, EmptyPlaceholderIsPathAndContextOverloadResolvesRoot) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  // {} is find's full-path placeholder -> an alias for {path}.
  EXPECT_THAT(Render("{}", "a/b/c.txt", md, 0), "a/b/c.txt");
  EXPECT_THAT(Render("echo {}", "p", md, 0), "echo p");
  // The context overload resolves {root}, which the rootless 4-arg overload cannot.
  EXPECT_THAT(
      Render("{root}:{}", RenderContext{.path = "r/sub/f", .root = "r", .metadata = md, .depth = 1}), "r:r/sub/f");
}

TEST_F(FieldsTest, NumericPlaceholdersRenderRegexCaptures) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::vector<std::string> captures = {"a/b/c.txt", "a/b", "c", "txt"};  // [0]=whole match, 1..3 groups
  const RenderContext ctx{.path = "a/b/c.txt", .metadata = md, .captures = captures};
  EXPECT_THAT(Render("{1}-{3}", ctx), "a/b-txt");
  EXPECT_THAT(Render("{0}", ctx), "a/b/c.txt");            // {0} is the whole match
  EXPECT_THAT(Render("{9}", ctx), "");                     // out of range -> empty
  EXPECT_THAT(Render("[{1}]", "a/b/c.txt", md, 0), "[]");  // no captures available -> empty
}

TEST_F(FieldsTest, EnvNamespaceReadsEnvironment) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  // {env.NAME} reads through the xff/env cache; inject values via its test seam rather than
  // mutating the real process environment.
  env::SetForTesting("XFF_TEST_ENV_VAR", "hello");
  EXPECT_THAT(Render("{env.XFF_TEST_ENV_VAR}", "p", md, 0), "hello");
  env::SetForTesting("XFF_TEST_ENV_VAR", std::nullopt);
  EXPECT_THAT(Render("{env.XFF_TEST_ENV_VAR}", "p", md, 0), "");  // unset -> empty
  env::ClearForTesting();
}

TEST_F(FieldsTest, ShardFieldRendersTheSetShardCount) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  // Present (a collapsed shard set): renders the shard count. Absent (a normal entry): empty, so
  // it no-ops in a template.
  EXPECT_THAT(Template::Compile("{shard}").Render(RenderContext{.path = "p", .metadata = md, .shard_count = 3}), "3");
  EXPECT_THAT(Template::Compile("[{shard}]").Render(RenderContext{.path = "p", .metadata = md}), "[]");
}

TEST_F(FieldsTest, FuzzyFieldRendersTheScoreOfTheLastFuzzyTest) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  // Present (a -fuzzy ran): renders the score. Absent (no fuzzy test in the expression): empty, so
  // it no-ops in a template, like {shard} outside shard mode.
  EXPECT_THAT(Template::Compile("{fuzzy}").Render(RenderContext{.path = "p", .metadata = md, .fuzzy_score = 65}), "65");
  EXPECT_THAT(Template::Compile("[{fuzzy}]").Render(RenderContext{.path = "p", .metadata = md}), "[]");
}

TEST_F(FieldsTest, DefNamespaceReadsDefines) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::map<std::string, std::string> defines = {{"greeting", "hi"}, {"n", "42"}};
  const RenderContext ctx{.path = "p", .metadata = md, .defines = defines};
  EXPECT_THAT(Render("{def.greeting}-{def.n}", ctx), "hi-42");
  EXPECT_THAT(Render("{def.missing}", ctx), "");              // undefined -> empty
  EXPECT_THAT(Render("[{def.greeting}]", "p", md, 0), "[]");  // no defines map -> empty
}

TEST_F(FieldsTest, OutputNamespaceReadsCaptureResults) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::map<std::string, std::string> outputs = {{"lines", "42"}};
  const RenderContext ctx{.path = "p", .metadata = md, .outputs = outputs};
  EXPECT_THAT(Render("{capture.lines}", ctx), "42");
  EXPECT_THAT(Render("[{capture.lines}]", ctx), "[42]");       // value composes with surrounding literals
  EXPECT_THAT(Render("{capture.missing}", ctx), "");           // unset -> empty
  EXPECT_THAT(Render("[{capture.lines}]", "p", md, 0), "[]");  // no outputs map -> empty
}

TEST_F(FieldsTest, RewriteQualifierTransformsTheValue) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  EXPECT_THAT(Render("{name:s/\\.txt$/.md/}", "a/b/c.txt", md, 0), "c.md");               // first match
  EXPECT_THAT(Render("{name:s/[aeiou]//g}", "a/b/foo.txt", md, 0), "f.txt");              // g = all matches
  EXPECT_THAT(Render("{path:s#/#_#g}", "a/b/c", md, 0), "a_b_c");                         // alternate delimiter
  EXPECT_THAT(Render("{size:h}", "f", Meta(vfs::FileType::kRegular, 1'536), 0), "1.5K");  // non-rewrite intact
}

TEST_F(FieldsTest, MExtractorYieldsAPerLineValueStream) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  // git-blame --line-porcelain-shaped output: many lines, an `author X` header per source line.
  const std::map<std::string, std::string> outputs = {
      {"blame", "author Bob\nauthor-mail <b@x>\nauthor Ann\n\tsource line\nauthor Bob\n"}};
  const Template compiled = Template::Compile("{capture.blame:m/^author (.+)$/\\1/}");
  const RenderContext ctx{.path = "f", .metadata = md, .outputs = outputs};
  // AsExtraction: one value per matching line, non-matching lines dropped, \1 = capture group.
  ASSERT_THAT(compiled.AsExtraction(ctx), Optional(_));
  EXPECT_THAT(compiled.AsExtraction(ctx), Optional(ElementsAre("Bob", "Ann", "Bob")));
  // Scalar Render projects the stream as the matches newline-joined.
  EXPECT_THAT(compiled.Render(ctx), "Bob\nAnn\nBob");
}

TEST_F(FieldsTest, MExtractorHonorsDelimiterFlagsAndWholeMatch) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::map<std::string, std::string> outputs = {{"x", "AUTHOR Bob\nnope\nauthor Ann"}};
  const RenderContext ctx{.path = "f", .metadata = md, .outputs = outputs};
  // Alternate ',' delimiter + case-insensitive 'i' flag; \1 keeps the name.
  EXPECT_THAT(
      Template::Compile("{capture.x:m,^author (.+)$,\\1,i}").AsExtraction(ctx), Optional(ElementsAre("Bob", "Ann")));
  // \0 keeps the whole matching line; a non-matching line is dropped.
  EXPECT_THAT(Template::Compile("{capture.x:m/nope/\\0/}").AsExtraction(ctx), Optional(ElementsAre("nope")));
}

TEST_F(FieldsTest, RewriteChainAppliesCommandsInSequence) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  // A ;-separated s-chain applies each substitution left to right; a command after ; may omit the
  // leading s (it defaults to a substitution).
  EXPECT_THAT(Render("{name:s/o/0/g;s/txt/md/}", "a/foo.txt", md, 0), "f00.md");
  EXPECT_THAT(Render("{name:s/o/0/g;/txt/md/}", "a/foo.txt", md, 0), "f00.md");    // 2nd command letterless
  EXPECT_THAT(Render("{path:s#/#_#g;s/^/root_/}", "a/b/c", md, 0), "root_a_b_c");  // alt delimiter + chain
}

TEST_F(FieldsTest, MalformedRewriteChainsLeaveTheOriginalValueUntouched) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  EXPECT_THAT(Render("{name:s/}", "a/foo.txt", md, 0), "foo.txt");
  EXPECT_THAT(Render("{name:s/foo}", "a/foo.txt", md, 0), "foo.txt");
  EXPECT_THAT(Render("{name:s/foo/bar}", "a/foo.txt", md, 0), "foo.txt");
  EXPECT_THAT(Render("{name:s/[x/y/}", "a/foo.txt", md, 0), "foo.txt");
  EXPECT_THAT(Render("{name:s/foo/bar/;s}", "a/foo.txt", md, 0), "foo.txt");
  EXPECT_THAT(Render("{name:s/foo/bar/;x/y/z/}", "a/foo.txt", md, 0), "foo.txt");
}

TEST_F(FieldsTest, TemplateParserPreservesMalformedAndEscapedPlaceholdersLiterally) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  EXPECT_THAT(Render("before {name", "a/foo.txt", md, 0), "before {name");
  EXPECT_THAT(Render(R"(before {name:"unterminated})", "a/foo.txt", md, 0), R"(before {name:"unterminated})");
  EXPECT_THAT(Render(R"({name:"a\\b\"c"})", "a/foo.txt", md, 0), "foo.txt");
  EXPECT_THAT(Render("{{{name}}}", "a/foo.txt", md, 0), "{foo.txt}");
}

TEST_F(FieldsTest, MExtractorChainFiltersThenSubstitutes) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::map<std::string, std::string> outputs = {{"blame", "author Bob Smith\nother line\nauthor Ann Lee\n"}};
  // First command extracts the author (and filters non-author lines); the second normalizes the
  // extracted value (spaces -> underscores) per surviving line.
  const Template compiled = Template::Compile("{capture.blame:m/^author (.+)$/\\1/;s/ /_/g}");
  const RenderContext ctx{.path = "f", .metadata = md, .outputs = outputs};
  ASSERT_THAT(compiled.AsExtraction(ctx), Optional(_));
  EXPECT_THAT(compiled.AsExtraction(ctx), Optional(ElementsAre("Bob_Smith", "Ann_Lee")));
}

TEST_F(FieldsTest, MReducerJoinCollapsesTheStreamToAScalar) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::map<std::string, std::string> outputs = {{"blame", "author Bob\nx\nauthor Ann\nauthor Bob\n"}};
  const RenderContext ctx{.path = "f", .metadata = md, .outputs = outputs};
  // A terminal join(...) reducer collapses the per-line stream to one scalar, so the extraction is
  // valid in a scalar Render: bare join = "\n"; join(SEP) a custom separator; join() concatenates.
  EXPECT_THAT(Template::Compile("{capture.blame:m/^author (.+)$/\\1/;join(, )}").Render(ctx), "Bob, Ann, Bob");
  EXPECT_THAT(Template::Compile("{capture.blame:m/^author (.+)$/\\1/;join}").Render(ctx), "Bob\nAnn\nBob");
  EXPECT_THAT(Template::Compile("{capture.blame:m/^author (.+)$/\\1/;join()}").Render(ctx), "BobAnnBob");
  EXPECT_THAT(Template::Compile("{capture.blame:m/^author (.+)$/\\1/;join(\\t)}").Render(ctx), "Bob\tAnn\tBob");
  EXPECT_THAT(Template::Compile("{capture.blame:m/^author (.+)$/\\1/;join(\\n)}").Render(ctx), "Bob\nAnn\nBob");
  EXPECT_THAT(Template::Compile(R"({capture.blame:m/^author (.+)$/\1/;join(a\)b)})").Render(ctx), "Boba)bAnna)bBob");
}

TEST_F(FieldsTest, MReducerIsScalarValuedNotAStream) {
  // A reducer-terminated extraction is scalar, so it is NOT a value stream: IsExtraction /
  // HasUnreducedExtraction treat it as an ordinary scalar template (the #136 guard lets it into
  // scalar contexts), unlike the bare (unreduced) extraction.
  const Template bare = Template::Compile("{capture.b:m/(.+)/\\1/}");
  const Template reduced = Template::Compile("{capture.b:m/(.+)/\\1/;join(, )}");
  EXPECT_THAT(bare.HasUnreducedExtraction(), IsTrue());
  EXPECT_THAT(bare.IsExtraction(), IsTrue());
  EXPECT_THAT(reduced.HasUnreducedExtraction(), IsFalse());
  EXPECT_THAT(reduced.IsExtraction(), IsFalse());
}

TEST_F(FieldsTest, MReducerAppliesPerLineChainThenJoinsThenScalarChain) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::map<std::string, std::string> outputs = {{"blame", "author Bob Smith\nx\nauthor Ann Lee\n"}};
  const RenderContext ctx{.path = "f", .metadata = md, .outputs = outputs};
  // The uniform pipeline: extract+map per line (spaces -> _), join with ", ", then a scalar s/// on
  // the joined value (_ -> .). #135's per-line s/// after m// is preserved; the reducer is the pivot.
  EXPECT_THAT(
      Template::Compile("{capture.blame:m/^author (.+)$/\\1/;s/ /_/g;join(, );s/_/./g}").Render(ctx),
      "Bob.Smith, Ann.Lee");
}

TEST_F(FieldsTest, MExtractorEmptyStreamVersusNonExtractionTemplate) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::map<std::string, std::string> outputs = {{"x", "aaa\nbbb"}};
  const RenderContext ctx{.path = "f", .metadata = md, .outputs = outputs};
  // A well-formed m// that matches nothing is an empty stream (present, but no values).
  EXPECT_THAT(Template::Compile("{capture.x:m/zzz/\\0/}").AsExtraction(ctx), Optional(IsEmpty()));
  // Anything that is not exactly one m// field is not a value stream.
  EXPECT_THAT(Template::Compile("{name}").AsExtraction(ctx), Eq(std::nullopt));                  // scalar field
  EXPECT_THAT(Template::Compile("x {capture.x:m/./\\0/}").AsExtraction(ctx), Eq(std::nullopt));  // a literal present
  EXPECT_THAT(Template::Compile("literal").AsExtraction(ctx), Eq(std::nullopt));                 // one literal segment
}

// The --help=fields SOT guard: FieldDocs() must document exactly the renderable field
// names, so the help topic can never drift from the actual field table.
TEST_F(FieldsTest, FieldDocsCoverEveryRenderableField) {
  std::vector<std::string_view> documented;
  for (const FieldDoc& doc : FieldDocs()) {
    documented.push_back(doc.name);
    for (const std::string_view alias : doc.aliases) {
      documented.push_back(alias);
    }
  }
  std::vector<std::string_view> renderable;
  for (const std::string_view name : FieldNames()) {
    if (!name.empty()) {  // "" backs {} (an alias of {path}); asserted separately below
      renderable.push_back(name);
    }
  }
  EXPECT_THAT(documented, UnorderedElementsAreArray(renderable));
}

TEST_F(FieldsTest, EmptyNameIsThePathAlias) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  EXPECT_THAT(Render("{}", "a/b/c", md, 0), "a/b/c");                           // {} -> full path
  EXPECT_THAT(Render("{}", "a/b/c", md, 0), Render("{path}", "a/b/c", md, 0));  // same as {path}
}

TEST_F(FieldsTest, EveryFieldDocHasGroupHeaderAndSummary) {
  for (const FieldDoc& doc : FieldDocs()) {
    EXPECT_THAT(doc.group, Not(IsEmpty())) << "field {" << doc.name << "}";
    EXPECT_THAT(doc.header, Not(IsEmpty())) << "field {" << doc.name << "}";
    EXPECT_THAT(doc.summary, Not(IsEmpty())) << "field {" << doc.name << "}";
  }
}

TEST_F(FieldsTest, PathComponentKeywordsAreExposedForHelp) {
  EXPECT_THAT(PathComponentKeywords(), AllOf(Not(IsEmpty()), Contains("stem"), Contains("dir")));
}

TEST_F(FieldsTest, StructuredHelpDocumentsEverySyntaxFamily) {
  const FieldHelpDocs& docs = FieldSyntaxDocs();
  EXPECT_THAT(docs.brace_rules, Not(IsEmpty()));
  EXPECT_THAT(docs.dynamic_namespaces, Contains(Field(&FieldHelpRow::term, Eq("{env.NAME}"))));
  EXPECT_THAT(
      docs.qualifiers,
      Contains(
          AllOf(Field(&FieldHelpRow::term, Eq("{path:COMP}")), Field(&FieldHelpRow::description, HasSubstr("stem")))));
  EXPECT_THAT(docs.qualifier_pipeline, HasSubstr("left-to-right pipeline"));
  EXPECT_THAT(docs.qualifier_example, HasSubstr("extract"));
  EXPECT_THAT(docs.printf_note, HasSubstr("--help=-printf"));
}

TEST_F(FieldsTest, StructuredHelpRowsHaveTermsAndDescriptions) {
  const FieldHelpDocs& docs = FieldSyntaxDocs();
  for (const FieldHelpRow& row : docs.dynamic_namespaces) {
    EXPECT_THAT(row.term, Not(IsEmpty()));
    EXPECT_THAT(row.description, Not(IsEmpty()));
  }
  for (const FieldHelpRow& row : docs.qualifiers) {
    EXPECT_THAT(row.term, Not(IsEmpty()));
    EXPECT_THAT(row.description, Not(IsEmpty()));
  }
}

TEST_F(FieldsTest, DocumentationVocabulariesHaveStableStorage) {
  const auto docs = FieldDocs();
  const auto names = FieldNames();
  const auto components = PathComponentKeywords();
  const FieldHelpDocs* const help = &FieldSyntaxDocs();
  EXPECT_THAT(FieldDocs().data(), Eq(docs.data()));
  EXPECT_THAT(FieldNames().data(), Eq(names.data()));
  EXPECT_THAT(PathComponentKeywords().data(), Eq(components.data()));
  EXPECT_THAT(&FieldSyntaxDocs(), Eq(help));
}

TEST_F(FieldsTest, IsKnownFieldAcceptsVocabularyRejectsUnknown) {
  // Powers --columns validation: a builtin, a qualified name, a namespace, and a capture
  // index are known; a typo is not.
  EXPECT_THAT(IsKnownField("path"), IsTrue());
  EXPECT_THAT(IsKnownField("mtime:%Y"), IsTrue());  // a name with a :qualifier
  EXPECT_THAT(IsKnownField("env.HOME"), IsTrue());  // a dynamic namespace
  EXPECT_THAT(IsKnownField("def.B"), IsTrue());
  EXPECT_THAT(IsKnownField("0"), IsTrue());  // a {0}..{N} capture index
  EXPECT_THAT(IsKnownField("bogus"), IsFalse());
}

}  // namespace
}  // namespace xff::fields
