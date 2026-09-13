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

#include "xff/archive/phar_writer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "mbo/digest/digest.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/member_path.h"
#include "xff/archive/phar_reader.h"
#include "xff/vfs/mutations.h"

namespace xff::archive {
namespace {

constexpr std::string_view kPharStubMember = ".phar/stub.php";

namespace stdfs = ::std::filesystem;

// php-src writes a tar-based / zip-based phar's signature to this exact member path (phar_tar.c,
// phar_zip.c), which is what makes such a container unsafe to rewrite blindly.
constexpr std::string_view kPharSignatureMember = ".phar/signature.bin";

// The global-flag bit that says a signature follows the data section (php-src: PHAR_HDR_SIGNATURE).
constexpr std::uint32_t kGlobalFlagSigned = 0x00010000;

// The signature trailer: [digest][4 bytes type][the 4 bytes `GBMB`].
constexpr std::string_view kSignatureMagic = "GBMB";
constexpr std::size_t kSignatureTailBytes = 4 + 4;  // the type plus the magic

// php-src ext/phar/php_phar.h: the signature algorithm codes. Only the plain digests are here; the
// OpenSSL family (0x10 and up) signs with a private key, which xff cannot have.
constexpr std::uint32_t kSigMd5 = 0x0001;
constexpr std::uint32_t kSigSha1 = 0x0002;
constexpr std::uint32_t kSigSha256 = 0x0003;
constexpr std::uint32_t kSigSha512 = 0x0004;

std::string LittleEndian32(std::uint32_t value) {
  std::string out(4, '\0');
  for (std::size_t i = 0; i < 4; ++i) {
    out[i] = static_cast<char>((value >> (8U * i)) & 0xFFU);
  }
  return out;
}

std::uint32_t ReadLittleEndian32(std::string_view bytes) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << (8U * i);
  }
  return value;
}

// A fixed-size digest as raw bytes, which is how a phar stores its signature.
template<std::size_t N>
std::string RawDigest(const std::array<std::uint8_t, N>& digest) {
  std::string out;
  out.reserve(N);
  for (const std::uint8_t byte : digest) {
    out.push_back(static_cast<char>(byte));
  }
  return out;
}

// The signature for `body` (everything before the signature itself) under `type`, or nullopt when this
// build cannot produce it - an OpenSSL-signed phar, which needs the key that signed it.
std::optional<std::string> SignatureFor(std::uint32_t type, std::string_view body) {
  switch (type) {
    case kSigMd5: return RawDigest(mbo::digest::md5::Digest(body));
    case kSigSha1: return RawDigest(mbo::digest::sha1::Digest(body));
    case kSigSha256: return RawDigest(mbo::digest::sha256::Digest(body));
    case kSigSha512: return RawDigest(mbo::digest::sha512::Digest(body));
    default: return std::nullopt;
  }
}

std::string_view SignatureName(std::uint32_t type) {
  switch (type) {
    case kSigMd5: return "md5";
    case kSigSha1: return "sha1";
    case kSigSha256: return "sha256";
    case kSigSha512: return "sha512";
    default: return "openssl or unknown";
  }
}

absl::StatusOr<std::string> ReadWholeFile(const std::string& path) {
  // XFF_HOST_IO: PHAR writer consumes the explicitly selected host input file.
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return absl::UnavailableError(absl::StrCat("cannot read ", path));
  }
  std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) {
    return absl::UnavailableError(absl::StrCat("cannot read ", path));
  }
  return bytes;
}

// SELECT: which manifest entries survive the removal. A requested name that is not in the archive
// stops the whole rewrite (NotFound naming every such member): half a deletion is worse than none.
using PharMemberRefs = std::vector<std::reference_wrapper<const PharMemberLayout>>;

absl::StatusOr<PharMemberRefs> SelectSurvivors(
    std::string_view path,
    const PharLayout& layout,
    const std::vector<std::string>& members) {
  std::vector<bool> requested_found(members.size(), false);
  PharMemberRefs survivors;
  survivors.reserve(layout.members.size());
  for (const PharMemberLayout& member : layout.members) {
    bool drop = false;
    for (std::size_t i = 0; i < members.size(); ++i) {
      if (NormalizeMemberName(members[i]) == NormalizeMemberName(member.name)) {
        drop = true;
        requested_found[i] = true;
      }
    }
    if (!drop) {
      survivors.emplace_back(member);
    }
  }
  if (absl::c_all_of(requested_found, [](bool found) { return found; })) {
    return survivors;
  }
  std::vector<std::string> missing;
  for (std::size_t i = 0; i < members.size(); ++i) {
    if (!requested_found[i]) {
      missing.push_back(members[i]);
    }
  }
  return absl::NotFoundError(absl::StrCat("no such member in ", path, ": ", absl::StrJoin(missing, ", ")));
}

// REBUILD: the manifest's fixed header (member count, API version, global flags, alias and container
// metadata) is copied whole and only its count is patched; each surviving entry and each surviving
// member's stored bytes are copied verbatim, in the same order, which is what keeps the per-member
// compression and CRC valid without touching a codec.
std::string RebuildPhar(const std::string& bytes, const PharLayout& layout, const PharMemberRefs& survivors) {
  const std::size_t header_size = layout.entries_offset - layout.manifest_start;
  std::string manifest(bytes, layout.manifest_start, header_size);
  manifest.replace(0, 4, LittleEndian32(static_cast<std::uint32_t>(survivors.size())));
  std::string data;
  for (const std::reference_wrapper<const PharMemberLayout> member_ref : survivors) {
    const PharMemberLayout& member = member_ref.get();
    manifest.append(bytes, member.entry_offset, member.entry_size);
    data.append(bytes, member.data_offset, static_cast<std::size_t>(member.stored_size));
  }
  std::string rebuilt(bytes, 0, layout.manifest_length_at);  // the stub, up to the manifest length
  rebuilt.append(LittleEndian32(static_cast<std::uint32_t>(manifest.size())));
  rebuilt.append(manifest);
  rebuilt.append(data);
  return rebuilt;
}

// RE-SIGN: the signature is a digest over everything before it, so it cannot be copied - it has to
// be taken again over the file just rebuilt. A no-op for an unsigned phar.
absl::Status AppendSignature(
    std::string_view path,
    const std::string& bytes,
    const PharLayout& layout,
    std::string& rebuilt) {
  if ((layout.global_flags & kGlobalFlagSigned) == 0) {
    return absl::OkStatus();
  }
  if (bytes.size() < layout.data_end + kSignatureTailBytes
      || std::string_view(bytes).substr(bytes.size() - kSignatureMagic.size()) != kSignatureMagic) {
    return absl::DataLossError(
        absl::StrCat("phar ", path, " declares a signature but has no GBMB trailer to read its type from"));
  }
  const std::uint32_t type = ReadLittleEndian32(std::string_view(bytes).substr(bytes.size() - kSignatureTailBytes, 4));
  const std::optional<std::string> signature = SignatureFor(type, rebuilt);
  if (!signature.has_value()) {
    return absl::UnimplementedError(
        absl::StrCat(
            "phar ", path, " is signed with ", SignatureName(type),
            ", which xff cannot recompute, so removing a member would leave it unverifiable"));
  }
  rebuilt.append(*signature);
  rebuilt.append(LittleEndian32(type));
  rebuilt.append(kSignatureMagic);
  return absl::OkStatus();
}

}  // namespace

bool IsSignedTarOrZipPhar(const std::vector<Member>& members) {
  return absl::c_any_of(
      members, [](const Member& member) { return NormalizeMemberName(member.path) == kPharSignatureMember; });
}

namespace {
absl::Status ValidateStubRemoval(const PharLayout& layout, const std::vector<std::string>& members) {
  const bool has_stored_stub = absl::c_any_of(layout.members, [](const PharMemberLayout& member) {
    return NormalizeMemberName(member.name) == kPharStubMember;
  });
  if (!has_stored_stub && absl::c_any_of(members, [](std::string_view member) {
        return NormalizeMemberName(member) == kPharStubMember;
      })) {
    return absl::FailedPreconditionError("cannot remove .phar/stub.php: a native phar requires its executable stub");
  }

  return absl::OkStatus();
}

absl::Status PublishPharReplacement(std::string_view path, std::string_view bytes, const vfs::MutationPolicy& policy) {
  const stdfs::path target{std::string(path)};
  MBO_ASSIGN_OR_RETURN(const auto temporary, vfs::TemporaryOutput::Create(absl::StrCat(path, ".xff-rewrite"), policy));
  MBO_RETURN_IF_ERROR(temporary->Write(bytes));
  std::error_code error;
  // XFF_HOST_IO: reads original permissions before publishing the owned replacement.
  const auto mode = stdfs::status(target, error).permissions();
  if (error) {
    return absl::UnavailableError(error.message());
  }
  MBO_RETURN_IF_ERROR(temporary->SetPermissions(static_cast<unsigned int>(mode)));
  return temporary->Publish(path);
}

}  // namespace

absl::Status RemovePharMembersOfFile(
    std::string_view path,
    const std::vector<std::string>& members,
    const vfs::MutationPolicy& policy) {
  MBO_RETURN_IF_ERROR(policy.Delete());
  MBO_RETURN_IF_ERROR(policy.Write(true));
  if (members.empty()) {
    return absl::OkStatus();
  }
  const std::string path_string(path);
  MBO_ASSIGN_OR_RETURN(const std::string bytes, ReadWholeFile(path_string));
  MBO_ASSIGN_OR_RETURN(const PharLayout layout, ParsePharLayout(bytes));

  MBO_RETURN_IF_ERROR(ValidateStubRemoval(layout, members));

  // The three stages carry the layout facts between them: SELECT decides survival, REBUILD copies
  // the surviving bytes verbatim, RE-SIGN digests the result when the original was signed.
  MBO_ASSIGN_OR_RETURN(const PharMemberRefs survivors, SelectSurvivors(path, layout, members));
  std::string rebuilt = RebuildPhar(bytes, layout, survivors);
  MBO_RETURN_IF_ERROR(AppendSignature(path, bytes, layout, rebuilt));

  return PublishPharReplacement(path, rebuilt, policy);
}

}  // namespace xff::archive
