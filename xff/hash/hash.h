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

#ifndef XFF_HASH_HASH_H_
#define XFF_HASH_HASH_H_

#include <optional>
#include <string>
#include <string_view>

#include "absl/types/span.h"

// File and data digests, wrapping mbo::digest (spec-verified transcriptions) behind a
// name-keyed dispatch so the whole vocabulary (the `{hash}` field, the `-hash` action) shares
// one algorithm table. Algorithms are the fixed-size one-shot digests: md5, sha1, the SHA-2
// family (sha224/sha256/sha384/sha512/sha512_224/sha512_256), the SHA-3 family
// (sha3_224/sha3_256/sha3_384/sha3_512), blake2b/blake2b_256, and blake3. md5 and sha1 are
// collision-broken; they are offered for interop only. The XOFs (shake) and keyed MACs (hmac)
// are intentionally out of scope here.
namespace xff::hash {

// How a digest's raw bytes are rendered.
enum class Encoding {
  kHex,     // lowercase hex, like `sha256sum` / Python's hexdigest (the default)
  kBase64,  // standard padded base64 (RFC 4648), like a Subresource-Integrity hash body
};

// Parses an encoding name -- "hex" or "base64" -- returning nullopt for anything else.
std::optional<Encoding> ParseEncoding(std::string_view name);

// Which global defaults a parsed hash request consumes. Explicit per-use settings do not consume them.
struct DefaultUsage {
  bool algorithm = false;
  bool encoding = false;
};

// A resolved (algorithm, encoding) pair, e.g. from a `{hash:...}` qualifier or a `-hash:...` spec.
struct AlgoEncoding {
  std::string_view algo;
  Encoding encoding = Encoding::kHex;
  DefaultUsage defaults;
};

// Parses an "ALGO[/ENCODING]" spec, filling empty parts from the defaults: an empty ALGO uses
// `default_algo`, an absent `/ENCODING` uses `default_encoding`. Returns nullopt if the resolved
// algorithm is unsupported or the ENCODING is not hex/base64. This is the single grammar shared by
// the `{hash}` field and the `-hash` action. The returned `algo` view aliases `spec` or
// `default_algo`, so keep those alive while using the result.
std::optional<AlgoEncoding> ParseSpec(
    std::string_view spec,
    std::string_view default_algo,
    Encoding default_encoding = Encoding::kHex);

// True if `algo` names a supported digest algorithm (one of AlgorithmNames()).
bool IsAlgorithm(std::string_view algo);

// The supported algorithm names, sorted, for `--help` and error messages.
absl::Span<const std::string_view> AlgorithmNames();

// The digest of `data` under `algo`, rendered per `encoding`; nullopt if `algo` is not a
// supported algorithm.
std::optional<std::string> HashData(std::string_view algo, std::string_view data, Encoding encoding = Encoding::kHex);

// Reads the file at `path` and returns its digest (see HashData); nullopt if `algo` is
// unsupported or the file cannot be read. The whole file is read into memory (streaming is a
// future refinement), so this is a Cost::kExpensive operation on the caller's side.
std::optional<std::string> HashFile(std::string_view algo, std::string_view path, Encoding encoding = Encoding::kHex);

}  // namespace xff::hash

#endif  // XFF_HASH_HASH_H_
