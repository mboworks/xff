// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "nlohmann/json.hpp"
#include "xff/presentation/render/render.h"

namespace xff::render {
namespace {
using ::mbo::testing::EqualsText;
using ::testing::Eq;

// Unicode-specific rendering tests. Ordinary rendering fixtures remain ASCII.
struct UnicodeRenderTest : ::testing::Test {};

TEST_F(UnicodeRenderTest, MultibyteLabelsSurviveTableBufferingAndTreeRendering) {
  // U+00E9 is Latin e with acute; this test exercises UTF-8 preservation only.
  for (const std::size_t window : std::to_array<std::size_t>({0, 1, TableStream::kAll})) {
    TableStream stream(Format::kAligned, {"name"}, false, window);
    std::string out = stream.Add({"café"});
    out += stream.Flush();
    EXPECT_THAT(out, EqualsText("café\n"));
  }
  Tree tree(false);
  tree.Add("root/café");
  EXPECT_THAT(tree.Render(), EqualsText("root\n`-- café\n"));
}

TEST_F(UnicodeRenderTest, JsonValuesPreserveUtf8AndEncodeOtherBytesLosslessly) {
  EXPECT_THAT(JsonValue("café"), Eq("\"café\""));
  EXPECT_THAT(JsonValue(std::string("x\xff", 2)), Eq(R"({"encoding":"base64","data":"eP8="})"));
  const auto malformed =
      std::to_array<std::string_view>({"\x80", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82"});
  for (const auto bytes : malformed) {
    const auto value = nlohmann::json::parse(JsonValue(bytes));
    EXPECT_THAT(value.at("encoding").get<std::string>(), Eq("base64"));
  }
  const auto path = nlohmann::json::parse(Renderer(Format::kJsonl).Record(std::string("x\xff", 2)));
  EXPECT_THAT(path.at("path").at("data").get<std::string>(), Eq("eP8="));
}

}  // namespace
}  // namespace xff::render
