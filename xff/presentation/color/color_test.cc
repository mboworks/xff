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

#include "xff/presentation/color/color.h"

#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/vfs/entry.h"

namespace xff::color {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct ColorTest : ::testing::Test {};

TEST_F(ColorTest, ResolveWhenLastOccurrenceWins) {
  EXPECT_THAT(ResolveWhen({}), Eq(When::kAuto));  // default
  EXPECT_THAT(ResolveWhen({"--color"}), Eq(When::kAlways));
  EXPECT_THAT(ResolveWhen({"--color=always"}), Eq(When::kAlways));
  EXPECT_THAT(ResolveWhen({"--color=auto"}), Eq(When::kAuto));
  EXPECT_THAT(ResolveWhen({"--color=never"}), Eq(When::kNever));
  EXPECT_THAT(ResolveWhen({"--color=always", "--color=never"}), Eq(When::kNever));
}

TEST_F(ColorTest, EnabledCombinesModeTtyAndNoColor) {
  EXPECT_THAT(
      Enabled(When::kAlways, /*stdout_is_tty=*/false, /*no_color_env=*/true), IsTrue());  // explicit wins over NO_COLOR
  EXPECT_THAT(Enabled(When::kNever, /*stdout_is_tty=*/true, /*no_color_env=*/false), IsFalse());
  EXPECT_THAT(Enabled(When::kAuto, /*stdout_is_tty=*/true, /*no_color_env=*/false), IsTrue());
  EXPECT_THAT(Enabled(When::kAuto, /*stdout_is_tty=*/false, /*no_color_env=*/false), IsFalse());  // not a terminal
  EXPECT_THAT(Enabled(When::kAuto, /*stdout_is_tty=*/true, /*no_color_env=*/true), IsFalse());    // NO_COLOR set
}

TEST_F(ColorTest, CodeForTypeUsesLsLikeScheme) {
  EXPECT_THAT(CodeForType(vfs::FileType::kDirectory, 0755), Eq("1;34"));
  EXPECT_THAT(CodeForType(vfs::FileType::kSymlink, 0777), Eq("1;36"));
  EXPECT_THAT(CodeForType(vfs::FileType::kFifo, 0644), Eq("33"));
  EXPECT_THAT(CodeForType(vfs::FileType::kRegular, 0755), Eq("1;32"));  // executable regular file
  EXPECT_THAT(CodeForType(vfs::FileType::kRegular, 0644), IsEmpty());   // plain file: no color
}

TEST_F(ColorTest, TheSchemeDefaultsToAutoAndTheFlagPicks) {
  // Default kAuto - ls OR xff: the theme when there is one, xff's scheme when there is not. The
  // spellings follow logic's algebra (`+` is OR, the merge is AND), and `default` names whatever the
  // default is, so a config file need not hard-code which scheme that currently is.
  EXPECT_THAT(ResolveScheme({}), Scheme::kAuto);
  EXPECT_THAT(ResolveScheme({"--color-scheme=auto"}), Scheme::kAuto);
  EXPECT_THAT(ResolveScheme({"--color-scheme=ls+xff"}), Scheme::kAuto);
  EXPECT_THAT(ResolveScheme({"--color-scheme=ls-or-xff"}), Scheme::kAuto);
  EXPECT_THAT(ResolveScheme({"--color-scheme=default"}), Scheme::kAuto);
  EXPECT_THAT(ResolveScheme({"--color-scheme=ls"}), Scheme::kLs);
  EXPECT_THAT(ResolveScheme({"--color-scheme=ls-and-xff"}), Scheme::kLsAndXff);
  EXPECT_THAT(ResolveScheme({"--color-scheme=merged"}), Scheme::kLsAndXff);  // the plain word for it
  EXPECT_THAT(ResolveScheme({"--color-scheme=xff"}), Scheme::kXff);
  EXPECT_THAT(ResolveScheme({"--color-scheme=xff", "--color-scheme=ls"}), Scheme::kLs);  // last wins
  EXPECT_THAT(ResolveScheme({"--color-scheme=nonsense"}), Scheme::kAuto);                // unknown leaves the default
  // `ls&xff` is not a spelling: an unquoted `&` backgrounds the command, so it must not silently work.
  EXPECT_THAT(ResolveScheme({"--color-scheme=ls&xff"}), Scheme::kAuto);
}

TEST_F(ColorTest, LsAloneLeavesWhatTheThemeOmitsUncoloured) {
  // The difference between the two ls readings, in one assertion pair: under `ls` a type the theme
  // never mentions prints plain (what a real ls does), under `ls-and-xff` it keeps xff's colour.
  const Palette strict = PaletteFor(Scheme::kLs, "di=01;35");
  EXPECT_THAT(strict.CodeFor("dir", vfs::FileType::kDirectory, 0755), "01;35");
  EXPECT_THAT(strict.CodeFor("link", vfs::FileType::kSymlink, 0777), IsEmpty());
  const Palette merged = PaletteFor(Scheme::kLsAndXff, "di=01;35");
  EXPECT_THAT(merged.CodeFor("link", vfs::FileType::kSymlink, 0777), CodeForType(vfs::FileType::kSymlink, 0777));
}

TEST_F(ColorTest, AutoDecidesPerVariableNotPerKey) {
  // `auto` is the third reading: a theme that is set at all is the whole answer, an unset one leaves
  // xff's scheme entirely intact.
  const Palette themed = PaletteFor(Scheme::kAuto, "di=01;35");
  EXPECT_THAT(themed.CodeFor("dir", vfs::FileType::kDirectory, 0755), "01;35");
  EXPECT_THAT(themed.CodeFor("link", vfs::FileType::kSymlink, 0777), IsEmpty());  // theme alone
  const Palette unset = PaletteFor(Scheme::kAuto, "");
  EXPECT_THAT(unset.CodeFor("link", vfs::FileType::kSymlink, 0777), CodeForType(vfs::FileType::kSymlink, 0777));
  EXPECT_THAT(unset.CodeFor("dir", vfs::FileType::kDirectory, 0755), CodeForType(vfs::FileType::kDirectory, 0755));
}

TEST_F(ColorTest, TheXffSchemeIgnoresTheThemeEntirely) {
  const Palette palette = PaletteFor(Scheme::kXff, "di=01;35:*.txt=33");
  EXPECT_THAT(palette.CodeFor("dir", vfs::FileType::kDirectory, 0755), CodeForType(vfs::FileType::kDirectory, 0755));
  EXPECT_THAT(palette.CodeFor("a.txt", vfs::FileType::kRegular, 0644), IsEmpty());
}

TEST_F(ColorTest, ADefaultPaletteIsTheBuiltInScheme) {
  const Palette palette;
  EXPECT_THAT(palette.CodeFor("dir", vfs::FileType::kDirectory, 0755), CodeForType(vfs::FileType::kDirectory, 0755));
  EXPECT_THAT(palette.CodeFor("a.txt", vfs::FileType::kRegular, 0644), IsEmpty());
}

TEST_F(ColorTest, LanguageColorIsTheBuiltInRegularFileFallback) {
  constexpr std::string_view kLanguageColor = "38;2;49;120;198";
  const Palette builtin = PaletteFor(Scheme::kXff, "*.cc=31");
  EXPECT_THAT(builtin.CodeFor("main.cc", vfs::FileType::kRegular, 0644, kLanguageColor), kLanguageColor);
  EXPECT_THAT(
      builtin.CodeFor("main.cc", vfs::FileType::kRegular, 0755, kLanguageColor),
      CodeForType(vfs::FileType::kRegular, 0755));
  EXPECT_THAT(
      builtin.CodeFor("src", vfs::FileType::kDirectory, 0755, kLanguageColor),
      CodeForType(vfs::FileType::kDirectory, 0755));
}

TEST_F(ColorTest, ThemePolicyControlsWhetherLanguageColorMayFallBack) {
  constexpr std::string_view kLanguageColor = "38;2;49;120;198";
  const Palette strict = PaletteFor(Scheme::kLs, "di=34");
  EXPECT_THAT(strict.CodeFor("main.cc", vfs::FileType::kRegular, 0644, kLanguageColor), IsEmpty());

  const Palette merged = PaletteFor(Scheme::kLsAndXff, "di=34");
  EXPECT_THAT(merged.CodeFor("main.cc", vfs::FileType::kRegular, 0644, kLanguageColor), kLanguageColor);

  const Palette extension = PaletteFor(Scheme::kLsAndXff, "*.cc=31");
  EXPECT_THAT(extension.CodeFor("main.cc", vfs::FileType::kRegular, 0644, kLanguageColor), "31");

  const Palette explicit_plain = PaletteFor(Scheme::kLsAndXff, "fi=");
  EXPECT_THAT(explicit_plain.CodeFor("main.cc", vfs::FileType::kRegular, 0644, kLanguageColor), IsEmpty());
}

TEST_F(ColorTest, LsColorsOverridesTheTypeItNames) {
  const Palette palette = Palette::FromLsColors("di=01;35:ln=04;36");
  EXPECT_THAT(palette.CodeFor("dir", vfs::FileType::kDirectory, 0755), "01;35");
  EXPECT_THAT(palette.CodeFor("link", vfs::FileType::kSymlink, 0777), "04;36");
  // A type $LS_COLORS says nothing about keeps the built-in colour rather than losing it.
  EXPECT_THAT(palette.CodeFor("pipe", vfs::FileType::kFifo, 0644), CodeForType(vfs::FileType::kFifo, 0644));
}

TEST_F(ColorTest, AnExtensionEntryColoursARegularFileAndFoldsCase) {
  const Palette palette = Palette::FromLsColors("*.tar=01;31:*.md=32");
  EXPECT_THAT(palette.CodeFor("archive.tar", vfs::FileType::kRegular, 0644), "01;31");
  EXPECT_THAT(palette.CodeFor("ARCHIVE.TAR", vfs::FileType::kRegular, 0644), "01;31");  // themed both ways
  EXPECT_THAT(palette.CodeFor("notes.md", vfs::FileType::kRegular, 0644), "32");
  EXPECT_THAT(palette.CodeFor("notes.rst", vfs::FileType::kRegular, 0644), IsEmpty());
}

TEST_F(ColorTest, TheExecutableBitWinsOverAnExtension) {
  // ls's own order: `ex` is consulted before the extension table, so an executable `*.sh` is
  // coloured as an executable rather than as a script file.
  const Palette palette = Palette::FromLsColors("ex=01;32:*.sh=33");
  EXPECT_THAT(palette.CodeFor("run.sh", vfs::FileType::kRegular, 0755), "01;32");
  EXPECT_THAT(palette.CodeFor("run.sh", vfs::FileType::kRegular, 0644), "33");
}

TEST_F(ColorTest, AnEmptyValueMeansNoColourRatherThanNoOpinion) {
  // `fi=` / `di=` is how a theme says "leave these plain"; it must not fall back to xff's scheme.
  const Palette palette = Palette::FromLsColors("di=:fi=");
  EXPECT_THAT(palette.CodeFor("dir", vfs::FileType::kDirectory, 0755), IsEmpty());
  EXPECT_THAT(palette.CodeFor("a.txt", vfs::FileType::kRegular, 0644), IsEmpty());
}

TEST_F(ColorTest, AMalformedLsColorsIsIgnoredEntryByEntry) {
  // A broken variable is not worth refusing to list files over, and ls ignores such entries too.
  const Palette palette = Palette::FromLsColors("garbage:di=01;35:=nokey:*=nodot:");
  EXPECT_THAT(palette.CodeFor("dir", vfs::FileType::kDirectory, 0755), "01;35");
  EXPECT_THAT(palette.CodeFor("a.txt", vfs::FileType::kRegular, 0644), IsEmpty());
}

TEST_F(ColorTest, BsdLsColorsMapsLetterPairsInTheirFixedOrder) {
  // macOS's default LSCOLORS. Position is the key: pair 1 is `di`, pair 2 is `ln`, and so on.
  const Palette palette = Palette::FromBsdLsColors("exfxcxdxbxegedabagacad");
  EXPECT_THAT(palette.CodeFor("dir", vfs::FileType::kDirectory, 0755), "34");       // `ex` -> blue on default
  EXPECT_THAT(palette.CodeFor("link", vfs::FileType::kSymlink, 0777), "35");        // `fx` -> magenta
  EXPECT_THAT(palette.CodeFor("sock", vfs::FileType::kSocket, 0644), "32");         // `cx` -> green
  EXPECT_THAT(palette.CodeFor("pipe", vfs::FileType::kFifo, 0644), "33");           // `dx` -> brown
  EXPECT_THAT(palette.CodeFor("run", vfs::FileType::kRegular, 0755), "31");         // `bx` -> red
  EXPECT_THAT(palette.CodeFor("blk", vfs::FileType::kBlockDevice, 0644), "34;46");  // `eg` -> blue on cyan
}

TEST_F(ColorTest, BsdUppercaseIsBoldForegroundAndBrightBackground) {
  const Palette palette = Palette::FromBsdLsColors("ExfBxxxxxxxxxxxxxxxxxx");
  // Brightness is spelled differently per role, as BSD ls renders it: bold for a foreground, the
  // high-intensity range for a background.
  EXPECT_THAT(palette.CodeFor("dir", vfs::FileType::kDirectory, 0755), "1;34");   // `Ex` -> bold blue
  EXPECT_THAT(palette.CodeFor("link", vfs::FileType::kSymlink, 0777), "35;101");  // `fB` -> on bright red
}

TEST_F(ColorTest, BsdDefaultPairIsTheTerminalDefault) {
  // `xx` is BSD's only way of saying "leave this type alone". Unlike $LS_COLORS's empty value it is
  // not a deliberate silence - every position is always present - so under `merged` it reads as "no
  // opinion" and xff's colour stands; under `ls` it prints plain, as ls does.
  const Palette palette = Palette::FromBsdLsColors("xxxxxxxxxxxxxxxxxxxxxx", /*fall_back=*/false);
  EXPECT_THAT(palette.CodeFor("dir", vfs::FileType::kDirectory, 0755), IsEmpty());
  EXPECT_THAT(palette.CodeFor("pipe", vfs::FileType::kFifo, 0644), IsEmpty());
  EXPECT_THAT(
      Palette::FromBsdLsColors("xxxxxxxxxxxxxxxxxxxxxx").CodeFor("dir", vfs::FileType::kDirectory, 0755),
      CodeForType(vfs::FileType::kDirectory, 0755));
}

TEST_F(ColorTest, BsdFallBackKeepsXffsSchemeForWhatTheVariableCannotSay) {
  // LSCOLORS has no per-extension entries and no `fi` slot, so `merged` is the interesting scheme
  // here: regular files keep xff's colours because the variable simply cannot speak about them.
  const Palette palette = Palette::FromBsdLsColors("exfxcxdxbxegedabagacad", /*fall_back=*/true);
  EXPECT_THAT(palette.CodeFor("dir", vfs::FileType::kDirectory, 0755), "34");
  EXPECT_THAT(palette.CodeFor("a.txt", vfs::FileType::kRegular, 0644), CodeForType(vfs::FileType::kRegular, 0644));
}

TEST_F(ColorTest, AMisSizedBsdLsColorsIsIgnoredWhole) {
  // A short value would shift every later type's colour by a position, which is worse than no theme
  // at all - so unlike $LS_COLORS (entry by entry), a malformed $LSCOLORS is dropped entirely.
  EXPECT_THAT(
      Palette::FromBsdLsColors("exfx", /*fall_back=*/false).CodeFor("dir", vfs::FileType::kDirectory, 0755), IsEmpty());
  // Long, and with a garbage tail: still all or nothing.
  EXPECT_THAT(
      Palette::FromBsdLsColors("exfxcxdxbxegedabagacadZZ", /*fall_back=*/false)
          .CodeFor("dir", vfs::FileType::kDirectory, 0755),
      IsEmpty());
  // Under `merged` an ignored variable is simply no theme, so xff's own scheme stands.
  EXPECT_THAT(
      Palette::FromBsdLsColors("exfx").CodeFor("dir", vfs::FileType::kDirectory, 0755),
      CodeForType(vfs::FileType::kDirectory, 0755));
  // And `auto` must reach xff's scheme too rather than treating "unreadable" as "plain".
  EXPECT_THAT(
      PaletteFor(Scheme::kAuto, "", "exfx").CodeFor("dir", vfs::FileType::kDirectory, 0755),
      CodeForType(vfs::FileType::kDirectory, 0755));
}

TEST_F(ColorTest, LsColorsWinsOverLscolorsWhenBothAreSet) {
  const Palette both = PaletteFor(Scheme::kLs, "di=01;35", "exfxcxdxbxegedabagacad");
  EXPECT_THAT(both.CodeFor("dir", vfs::FileType::kDirectory, 0755), "01;35");
  const Palette bsd_only = PaletteFor(Scheme::kLs, "", "exfxcxdxbxegedabagacad");
  EXPECT_THAT(bsd_only.CodeFor("dir", vfs::FileType::kDirectory, 0755), "34");
}

TEST_F(ColorTest, AutoTakesTheBsdThemeWhenThatIsTheOnlyOneSet) {
  // The macOS case: no dircolors setup at all, yet `ls` is themed - `auto` must follow it.
  const Palette palette = PaletteFor(Scheme::kAuto, "", "exfxcxdxbxegedabagacad");
  EXPECT_THAT(palette.CodeFor("dir", vfs::FileType::kDirectory, 0755), "34");
  EXPECT_THAT(palette.CodeFor("a.txt", vfs::FileType::kRegular, 0644), IsEmpty());
  // With neither variable set `auto` is xff's own scheme, unchanged.
  EXPECT_THAT(
      PaletteFor(Scheme::kAuto, "", "").CodeFor("dir", vfs::FileType::kDirectory, 0755),
      CodeForType(vfs::FileType::kDirectory, 0755));
}

}  // namespace
}  // namespace xff::color
