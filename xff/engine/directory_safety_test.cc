// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/config/config.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/local_fs.h"
#include "xff/vfs/mutations.h"

namespace xff::engine {
namespace {
using ::mbo::testing::IsOk;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct DirectorySafetyTest : ::testing::Test {
  struct Outcome {
    std::vector<std::string> records;
    std::vector<std::string> errors;
  };

  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(
        root_, vfs::TemporaryDirectory::Create(
                   (std::filesystem::temp_directory_path() / "xff-directory-engine").string(), {}));
    root_path_ = std::filesystem::canonical(root_->Path()).string();
    std::filesystem::create_directory(Path("temp"));
    std::filesystem::create_directory(Path("output"));
    std::ofstream(Path("input")) << "source";
  }

  std::string Path(std::string_view name) const { return root_path_ + "/" + std::string(name); }

  std::string Policy() const {
    return "--detailed-block-policy=archive,temp,output\n--temp-root=" + Path("temp")
           + "\n--output-root=" + Path("output") + "\n--block-file-writing\n--block-file-deletion\n";
  }

  static Outcome Run(std::string_view ini, const std::vector<std::string>& argv) {
    auto parsed = parser::Parse(argv);
    EXPECT_THAT(parsed, IsOk());
    if (!parsed.ok()) {
      return {.errors = {parsed.status().ToString()}};
    }
    config::ConfigInputs inputs;
    inputs.system = config::ParseIni(ini);
    const auto flags = config::ResolveConfigInOrder(inputs, parsed->globals, "xff");
    parsed->globals.clear();
    for (const auto& flag : flags) {
      parsed->globals.push_back(flag.flag);
    }
    parsed->safety_flags_expanded = true;
    parser::BindMatchers(
        *parsed, parser::GrammarFromGlobals(parsed->globals),
        parser::ResolveCaseMode(parsed->globals, registry::Style::kXff));
    Outcome result;
    const vfs::LocalFs filesystem;
    (void)RunFind(
        *parsed, filesystem, [&](std::string_view text) { result.records.emplace_back(text); },
        [&](std::string_view, const absl::Status& status) { result.errors.push_back(status.ToString()); });
    return result;
  }

  std::unique_ptr<vfs::TemporaryDirectory> root_;
  std::string root_path_;
};

TEST_F(DirectorySafetyTest, IniRootsPermitOutputAndScratchButKeepOrdinaryWritesBlocked) {
  for (const std::string_view name : std::to_array<std::string_view>({"temp/result", "output/result"})) {
    const auto result = Run(Policy(), {Path("input"), "-fprint", Path(name)});
    EXPECT_THAT(result.errors, IsEmpty());
    EXPECT_THAT(std::filesystem::exists(Path(name)), IsTrue());
  }
  const auto blocked = Run(Policy(), {Path("input"), "-fprint", Path("outside")});
  EXPECT_THAT(blocked.errors, Contains(HasSubstr("blocked writing")));
  EXPECT_THAT(std::filesystem::exists(Path("outside")), IsFalse());
}

TEST_F(DirectorySafetyTest, PreviewChecksOutputPolicyWithoutCreatingOrReplacingFiles) {
  const auto policy = Policy() + "--block-output-file-overwrite\n";
  EXPECT_THAT(Run(policy, {Path("input"), "-fprint", Path("output/result")}).errors, IsEmpty());
  EXPECT_THAT(
      Run(policy, {Path("input"), "--dry-run", "-fprint", Path("output/result")}).errors,
      Contains(HasSubstr("overwrite")));
  const auto preview = Run(policy, {Path("input"), "--dry-run", "-fprint", Path("output/new")});
  EXPECT_THAT(preview.errors, IsEmpty());
  EXPECT_THAT(preview.records, Contains(HasSubstr("output/new")));
  EXPECT_THAT(std::filesystem::exists(Path("output/new")), IsFalse());
  EXPECT_THAT(
      Run(policy, {Path("input"), "--dry-run", "-fprint", Path("output")}).errors,
      Contains(HasSubstr("root is protected")));
  EXPECT_THAT(
      Run(policy, {Path("input"), "--dry-run", "-fprint", Path("outside")}).errors,
      Contains(HasSubstr("blocked writing")));
}

TEST_F(DirectorySafetyTest, DeletionAndItsPreviewUseTheDestinationScope) {
  std::ofstream(Path("output/result")) << "keep";
  const auto preview = Run(Policy(), {Path("output/result"), "--dry-run", "-delete"});
  EXPECT_THAT(preview.errors, IsEmpty());
  EXPECT_THAT(std::filesystem::exists(Path("output/result")), IsTrue());
  EXPECT_THAT(Run(Policy(), {Path("output/result"), "-delete"}).errors, IsEmpty());
  EXPECT_THAT(std::filesystem::exists(Path("output/result")), IsFalse());
  EXPECT_THAT(Run(Policy(), {Path("input"), "-delete"}).errors, Contains(HasSubstr("blocked deletion")));
  EXPECT_THAT(std::filesystem::exists(Path("input")), IsTrue());
  std::filesystem::create_directory(Path("output/empty"));
  EXPECT_THAT(Run(Policy(), {Path("output/empty"), "--dry-run", "-delete"}).errors, IsEmpty());
  EXPECT_THAT(Run(Policy(), {Path("output/empty"), "-delete"}).errors, IsEmpty());
  EXPECT_THAT(std::filesystem::exists(Path("output/empty")), IsFalse());
}

TEST_F(DirectorySafetyTest, InvalidRootAndArchiveDestinationFailBeforeAnyMutation) {
  EXPECT_THAT(
      Run("--output-root=" + Path("missing"), {Path("input"), "-print"}).errors,
      Contains(HasSubstr("cannot traverse policy directory")));
  EXPECT_THAT(
      Run(Policy(), {Path("input"), "--dry-run", "--pack=" + Path("missing/archive.tar")}).errors,
      Contains(HasSubstr("cannot traverse policy directory")));
  EXPECT_THAT(
      Run(Policy(), {Path("input"), "--dry-run", "--pack=" + Path("outside.tar")}).errors,
      Contains(HasSubstr("blocked writing")));
  EXPECT_THAT(
      Run(Policy(), {Path("input"), "--dry-run", "--pack=" + Path("output/archive.tar")}).errors,
      Contains(HasSubstr("built without archive support")));
  EXPECT_THAT(std::filesystem::exists(Path("outside.tar")), IsFalse());
}
}  // namespace
}  // namespace xff::engine
