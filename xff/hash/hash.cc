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

#include "xff/hash/hash.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/types/span.h"
#include "mbo/container/limited_map.h"
#include "mbo/digest/digest.h"
#include "xff/vfs/local_fs.h"

namespace xff::hash {
namespace {

// Renders a fixed-size digest per `encoding`: lowercase hex (mbo's ToHexString) or standard
// padded base64 over the raw digest bytes.
template<std::size_t N>
std::string Encode(const std::array<uint8_t, N>& digest, Encoding encoding) {
  if (encoding == Encoding::kBase64) {
    // Converted element-wise rather than viewed through a cast: a digest is at most 64 bytes, so the
    // copy is nothing, and it keeps this file free of reinterpret_cast entirely (string_view has no
    // uint8_t form, so the only alternatives are this or the cast).
    return absl::Base64Escape(std::string(digest.begin(), digest.end()));
  }
  return mbo::digest::ToHexString(digest);
}

// One algorithm: hash `data` and render it per `encoding`. Captureless so the table below is a
// constexpr LimitedMap (like the engine's kDispatch); each wraps one mbo::digest namespace.
using DigestFn = std::string (*)(std::string_view data, Encoding encoding);

// The supported algorithms, keyed by name (MakeLimitedMap sorts by key). md5 / sha1 are
// collision-broken and offered for interop only. This is the single source of truth for the
// algorithm set: IsAlgorithm / AlgorithmNames / HashData all read it.
constexpr auto kAlgorithms = mbo::container::MakeLimitedMap(
    std::pair<std::string_view, DigestFn>{
        "blake2b",
        [](std::string_view data, Encoding encoding) { return Encode(mbo::digest::blake2b::Digest(data), encoding); }},
    std::pair<std::string_view, DigestFn>{
        "blake2b_256", [](std::string_view data,
                          Encoding encoding) { return Encode(mbo::digest::blake2b_256::Digest(data), encoding); }},
    std::pair<std::string_view, DigestFn>{
        "blake3",
        [](std::string_view data, Encoding encoding) { return Encode(mbo::digest::blake3::Digest(data), encoding); }},
    std::pair<std::string_view, DigestFn>{
        "md5",
        [](std::string_view data, Encoding encoding) { return Encode(mbo::digest::md5::Digest(data), encoding); }},
    std::pair<std::string_view, DigestFn>{
        "sha1",
        [](std::string_view data, Encoding encoding) { return Encode(mbo::digest::sha1::Digest(data), encoding); }},
    std::pair<std::string_view, DigestFn>{
        "sha224",
        [](std::string_view data, Encoding encoding) { return Encode(mbo::digest::sha224::Digest(data), encoding); }},
    std::pair<std::string_view, DigestFn>{
        "sha256",
        [](std::string_view data, Encoding encoding) { return Encode(mbo::digest::sha256::Digest(data), encoding); }},
    std::pair<std::string_view, DigestFn>{
        "sha3_224",
        [](std::string_view data, Encoding encoding) { return Encode(mbo::digest::sha3_224::Digest(data), encoding); }},
    std::pair<std::string_view, DigestFn>{
        "sha3_256",
        [](std::string_view data, Encoding encoding) { return Encode(mbo::digest::sha3_256::Digest(data), encoding); }},
    std::pair<std::string_view, DigestFn>{
        "sha3_384",
        [](std::string_view data, Encoding encoding) { return Encode(mbo::digest::sha3_384::Digest(data), encoding); }},
    std::pair<std::string_view, DigestFn>{
        "sha3_512",
        [](std::string_view data, Encoding encoding) { return Encode(mbo::digest::sha3_512::Digest(data), encoding); }},
    std::pair<std::string_view, DigestFn>{
        "sha384",
        [](std::string_view data, Encoding encoding) { return Encode(mbo::digest::sha384::Digest(data), encoding); }},
    std::pair<std::string_view, DigestFn>{
        "sha512",
        [](std::string_view data, Encoding encoding) { return Encode(mbo::digest::sha512::Digest(data), encoding); }},
    std::pair<std::string_view, DigestFn>{
        "sha512_224", [](std::string_view data,
                         Encoding encoding) { return Encode(mbo::digest::sha512_224::Digest(data), encoding); }},
    std::pair<std::string_view, DigestFn>{"sha512_256", [](std::string_view data, Encoding encoding) {
                                            return Encode(mbo::digest::sha512_256::Digest(data), encoding);
                                          }});

}  // namespace

std::optional<Encoding> ParseEncoding(std::string_view name) {
  if (name == "hex") {
    return Encoding::kHex;
  }
  if (name == "base64") {
    return Encoding::kBase64;
  }
  return std::nullopt;
}

std::optional<AlgoEncoding> ParseSpec(std::string_view spec, std::string_view default_algo, Encoding default_encoding) {
  std::string_view algo = spec;
  std::string_view encoding_name;
  if (const std::size_t slash = spec.find('/'); slash != std::string_view::npos) {
    algo = spec.substr(0, slash);
    encoding_name = spec.substr(slash + 1);
  }
  const DefaultUsage defaults{.algorithm = algo.empty(), .encoding = encoding_name.empty()};
  if (algo.empty()) {
    algo = default_algo;
  }
  if (!IsAlgorithm(algo)) {
    return std::nullopt;
  }
  Encoding encoding = default_encoding;
  if (!encoding_name.empty()) {
    const std::optional<Encoding> parsed = ParseEncoding(encoding_name);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    encoding = *parsed;
  }
  return AlgoEncoding{.algo = algo, .encoding = encoding, .defaults = defaults};
}

bool IsAlgorithm(std::string_view algo) {
  return kAlgorithms.contains(algo);
}

absl::Span<const std::string_view> AlgorithmNames() {
  // Built once from the (sorted) table, so the name list never drifts from kAlgorithms. The views
  // point into the constexpr kAlgorithms keys, which outlive everything.
  static constexpr auto kNames = [] {
    std::array<std::string_view, kAlgorithms.size()> names{};
    std::ranges::transform(kAlgorithms, names.begin(), [](const auto& algorithm) { return algorithm.first; });
    return names;
  }();
  return kNames;
}

std::optional<std::string> HashData(std::string_view algo, std::string_view data, Encoding encoding) {
  const auto it = kAlgorithms.find(algo);
  if (it == kAlgorithms.end()) {
    return std::nullopt;
  }
  return it->second(data, encoding);
}

std::optional<std::string> HashFile(std::string_view algo, std::string_view path, Encoding encoding) {
  if (!IsAlgorithm(algo)) {
    return std::nullopt;  // reject before touching the filesystem
  }
  const vfs::LocalFs fs;
  const absl::StatusOr<std::string> content = fs.ReadContent(path);
  if (!content.ok()) {
    return std::nullopt;
  }
  return HashData(algo, *content, encoding);
}

}  // namespace xff::hash
