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

#include "xff/datetime/datetime.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "mbo/container/limited_map.h"

namespace xff::datetime {
namespace {

// Adds `count` of a single time `unit` (plural already stripped) to a relative-time accumulator:
// fixed durations (second..week) into `offset`, calendar terms (month/year) into `months`. Returns
// false for an unknown unit.
bool AccumulateUnit(std::string_view unit, std::int64_t count, absl::Duration& offset, std::int64_t& months) {
  if (unit == "second" || unit == "sec") {
    offset += count * absl::Seconds(1);
  } else if (unit == "minute" || unit == "min") {
    offset += count * absl::Minutes(1);
  } else if (unit == "hour" || unit == "hr") {
    offset += count * absl::Hours(1);
  } else if (unit == "day") {
    offset += count * absl::Hours(24);
  } else if (unit == "week") {
    offset += count * absl::Hours(24 * 7);
  } else if (unit == "month") {
    months += count;
  } else if (unit == "year") {
    months += 12 * count;
  } else {
    return false;  // unknown unit
  }
  return true;
}

// Relative: "[+|-]N unit[s] [N unit[s] ...] [ago]" -- one or more count+unit terms summed into a
// single offset from `now`. A leading '-' on the first count OR a trailing "ago" puts the whole
// offset in the past; '+' or neither, the future. Calendar units (month/year) shift the civil date
// (preserving the time of day, in `tz`); the rest are fixed durations added on top. `lowered` is the
// already-lowercased input. Returns nullopt on any malformed term.
std::optional<absl::Time> ParseRelativeTime(std::string_view lowered, absl::Time now, absl::TimeZone tz) {
  std::vector<std::string_view> parts = absl::StrSplit(lowered, ' ', absl::SkipEmpty());
  bool past = false;
  if (!parts.empty() && parts.back() == "ago") {
    past = true;
    parts.pop_back();
  }
  if (parts.empty() || parts.size() % 2 != 0) {
    return std::nullopt;  // need whole count/unit pairs
  }
  if (parts.front().front() == '-') {  // a leading '-' selects the past for the whole sum
    past = true;
  }
  absl::Duration offset;    // accumulated fixed-duration terms (second..week)
  std::int64_t months = 0;  // accumulated calendar terms (month/year), in months
  for (std::size_t i = 0; i + 1 < parts.size(); i += 2) {
    std::int64_t count = 0;
    if (!absl::SimpleAtoi(parts[i], &count)) {
      return std::nullopt;
    }
    count = count < 0 ? -count : count;  // magnitude; direction comes from `past`
    std::string_view unit = parts[i + 1];
    if (unit.size() > 1 && unit.back() == 's') {
      unit.remove_suffix(1);  // accept the plural form
    }
    if (!AccumulateUnit(unit, count, offset, months)) {
      return std::nullopt;  // unknown unit
    }
  }
  const std::int64_t sign = past ? -1 : 1;
  absl::Time result = now;
  if (months != 0) {
    const absl::CivilSecond base = absl::ToCivilSecond(now, tz);
    const absl::CivilMonth shifted = absl::CivilMonth(base.year(), base.month()) + sign * months;
    result = absl::FromCivil(
        absl::CivilSecond(shifted.year(), shifted.month(), base.day(), base.hour(), base.minute(), base.second()), tz);
  }
  return result + sign * offset;
}

}  // namespace

std::optional<absl::Time> ParseTimeString(std::string_view text, absl::Time now, absl::TimeZone tz) {
  if (!text.empty() && text.front() == '@') {  // @epoch-seconds
    std::int64_t seconds = 0;
    if (!absl::SimpleAtoi(text.substr(1), &seconds)) {
      return std::nullopt;
    }
    return absl::FromUnixSeconds(seconds);
  }
  // Tried in order, most specific first: a bare date must not win over a full timestamp.
  static constexpr std::array kAcceptedFormats = std::to_array<std::string_view>({
      "%Y-%m-%dT%H:%M:%S",
      "%Y-%m-%d %H:%M:%S",
      "%Y-%m-%d",
  });
  for (const std::string_view format : kAcceptedFormats) {
    absl::Time time;
    std::string error;
    if (absl::ParseTime(format, text, tz, &time, &error)) {
      return time;
    }
  }
  const std::string lowered = absl::AsciiStrToLower(text);
  // Day keywords (find's get_date accepts these). "today" is the reference instant;
  // "yesterday"/"tomorrow" are one fixed day (24h) before/after it, consistent with
  // the relative-duration handling below (a "day" is 24h here; -daystart is the way
  // to anchor on civil midnight).
  if (lowered == "now" || lowered == "today") {
    return now;
  }
  if (lowered == "yesterday") {
    return now - absl::Hours(24);
  }
  if (lowered == "tomorrow") {
    return now + absl::Hours(24);
  }
  return ParseRelativeTime(lowered, now, tz);
}

absl::Time StartOfDay(absl::Time time, absl::TimeZone tz) {
  return absl::FromCivil(absl::ToCivilDay(time, tz), tz);  // midnight of time's civil day in tz
}

std::optional<absl::TimeZone> ParseTimeZone(std::string_view spec) {
  const std::string lowered = absl::AsciiStrToLower(spec);
  if (lowered.empty() || lowered == "local") {
    return absl::LocalTimeZone();
  }
  if (lowered == "utc" || lowered == "z" || lowered == "zulu") {
    return absl::UTCTimeZone();
  }
  // Fixed UTC offset: +HH, +HH:MM, or +HHMM (and the '-' forms). absl::LoadTimeZone
  // cannot parse these, so build an absl::FixedTimeZone from the parsed offset.
  if (spec.size() >= 2 && (spec.front() == '+' || spec.front() == '-')) {
    const int sign = spec.front() == '-' ? -1 : 1;
    const std::string_view rest = spec.substr(1);
    std::string_view hh = rest;
    std::string_view mm = "0";
    if (const auto colon = rest.find(':'); colon != std::string_view::npos) {
      hh = rest.substr(0, colon);
      mm = rest.substr(colon + 1);
    } else if (rest.size() == 4) {  // compact HHMM
      hh = rest.substr(0, 2);
      mm = rest.substr(2);
    }
    int hours = 0;
    int minutes = 0;
    if (absl::SimpleAtoi(hh, &hours) && absl::SimpleAtoi(mm, &minutes) && hours >= 0 && hours <= 23 && minutes >= 0
        && minutes <= 59) {
      return absl::FixedTimeZone(sign * ((hours * 3'600) + (minutes * 60)));
    }
    return std::nullopt;  // malformed offset
  }
  absl::TimeZone zone;
  return absl::LoadTimeZone(spec, &zone) ? std::optional(zone) : std::nullopt;
}

// Preset time formats; any other spec is used verbatim as an absl::FormatTime
// pattern. Keyed alphabetically (constexpr dispatch, like the engine's tables).
// Only genuinely-conformant forms carry a standard's name; "space" is the readable
// default and claims none. epoch/zulu/zulu-dense are handled in FormatTime (they
// are numeric or force UTC), so they are not table entries.
constexpr std::string_view kIso8601 = "%Y-%m-%dT%H:%M:%S%z";  // ISO-8601 extended, shared by "iso"/"iso8601"
constexpr std::string_view kSpace = "%Y-%m-%d %H:%M:%S %z";   // readable default, shared by "space"/"human"

constexpr auto kNamedFormats = mbo::container::MakeLimitedMap(
    std::pair<std::string_view, std::string_view>{"asctime", "%a %b %e %H:%M:%S %Y"},   // asctime(3); find default %t
    std::pair<std::string_view, std::string_view>{"human", kSpace},                     // alias of space
    std::pair<std::string_view, std::string_view>{"iso", kIso8601},                     // shorthand for iso8601
    std::pair<std::string_view, std::string_view>{"iso8601", kIso8601},                 // ISO-8601 extended (T)
    std::pair<std::string_view, std::string_view>{"iso8601-basic", "%Y%m%dT%H%M%S%z"},  // ISO-8601 basic (compact)
    std::pair<std::string_view, std::string_view>{"iso8601-full", "%Y-%m-%dT%H:%M:%E9S%z"},  // ISO-8601 + sub-second
    std::pair<std::string_view, std::string_view>{"rfc3339", "%Y-%m-%dT%H:%M:%S%Ez"},        // RFC 3339 (colon offset)
    std::pair<std::string_view, std::string_view>{"space", kSpace});  // readable default (primary)

namespace {

// True if `pattern` already carries a zone token (`%z`, `%Ez`, or `%Z`). Note `%Ez` does
// not contain `%z` as a substring (the 'E' is between), so both are checked.
bool HasZoneToken(std::string_view pattern) {
  return absl::StrContains(pattern, "%z") || absl::StrContains(pattern, "%Ez") || absl::StrContains(pattern, "%Z");
}

// Drops a trailing zone token (`%Ez` for rfc3339, else `%z`) and the separator space that
// precedes it in the readable "space" preset, so the preset renders with no zone suffix.
std::string WithoutZoneSuffix(std::string_view pattern) {
  std::string out(pattern);
  if (absl::EndsWith(out, "%Ez")) {
    out.resize(out.size() - 3);
  } else if (absl::EndsWith(out, "%z")) {
    out.resize(out.size() - 2);
  } else {
    return out;  // no trailing zone token (e.g. asctime)
  }
  if (absl::EndsWith(out, " ")) {
    out.pop_back();
  }
  return out;
}

}  // namespace

std::string FormatTime(absl::Time time, std::string_view spec, absl::TimeZone tz, ZoneSuffix suffix) {
  if (spec == "epoch") {
    return std::to_string(absl::ToUnixSeconds(time));  // seconds; no zone to add or drop
  }
  // zulu / zulu-dense are UTC-by-definition: the 'Z' is the format's identity, so the
  // suffix control never removes it (--time-zone-suffix=never leaves them as-is).
  if (spec == "zulu") {  // UTC with a 'Z' designator (extended), regardless of `tz`
    return absl::FormatTime("%Y-%m-%dT%H:%M:%SZ", time, absl::UTCTimeZone());
  }
  if (spec == "zulu-dense") {  // UTC 'Z', no separators (compact)
    return absl::FormatTime("%Y%m%dT%H%M%SZ", time, absl::UTCTimeZone());
  }
  // ASN.1 GeneralizedTime (X.680) YYYYMMDDHHMMSS in local time, no separators. Its zone is
  // OPTIONAL: --time-zone-suffix=always appends the numeric offset ASN.1-style (no separator,
  // `+0100`), never/auto leave it bare. The UTC 'Z' form is the separate "asn1z" preset.
  if (spec == "asn1" || spec == "generalizedtime") {
    std::string pattern = "%Y%m%d%H%M%S";
    if (suffix == ZoneSuffix::kAlways) {
      absl::StrAppend(&pattern, "%z");
    }
    return absl::FormatTime(pattern, time, tz);
  }
  // ASN.1 GeneralizedTime, UTC with a mandatory 'Z' - inherently zoned, so `suffix` is ignored
  // (like zulu): the 'Z' is the format's identity and is never dropped or offset.
  if (spec == "asn1z") {
    return absl::FormatTime("%Y%m%d%H%M%SZ", time, absl::UTCTimeZone());
  }
  const std::string_view name = spec.empty() ? std::string_view("space") : spec;
  const auto it = kNamedFormats.find(name);
  if (it == kNamedFormats.end()) {
    return absl::FormatTime(name, time, tz);  // a custom pattern is verbatim; `suffix` never touches it
  }
  std::string pattern(it->second);  // a preset name resolves to its pattern
  if (suffix == ZoneSuffix::kNever) {
    pattern = WithoutZoneSuffix(pattern);
  } else if (suffix == ZoneSuffix::kAlways && !HasZoneToken(pattern)) {
    absl::StrAppend(&pattern, " %z");  // force an offset onto a preset that omits one (asctime)
  }
  return absl::FormatTime(pattern, time, tz);
}

absl::Span<const std::string_view> NamedFormatNames() {
  static constexpr auto kNames = [] {
    std::array<std::string_view, kNamedFormats.size()> names{};
    std::ranges::transform(kNamedFormats, names.begin(), [](const auto& format) { return format.first; });
    return names;
  }();
  return kNames;
}

absl::Span<const std::pair<std::string_view, std::string_view>> FormatDocs() {
  static constexpr auto kDocs = std::to_array<std::pair<std::string_view, std::string_view>>({
      {"iso, iso8601", "ISO-8601 extended (2020-09-13T12:26:40+0000)"},
      {"iso8601-basic", "ISO-8601 basic / compact (20200913T122640+0000)"},
      {"iso8601-full", "ISO-8601 with sub-second precision"},
      {"rfc3339", "RFC 3339, colon offset (2020-09-13T12:26:40+00:00)"},
      {"space, human", "readable default (2020-09-13 12:26:40 +0000)"},
      {"asctime", "asctime(3); find's default %t (Sun Sep 13 12:26:40 2020)"},
      {"epoch", "seconds since the Unix epoch"},
      {"zulu", "UTC with a Z designator (2020-09-13T12:26:40Z)"},
      {"zulu-dense", "UTC Z, no separators (20200913T122640Z)"},
      {"asn1, generalizedtime", "ASN.1 GeneralizedTime, local (20200913122640); =always adds +0000"},
      {"asn1z", "ASN.1 GeneralizedTime, UTC Z (20200913122640Z)"},
      {"<strftime>", "any other value is used as an strftime(3) pattern, e.g. %Y-%m-%d"},
  });
  return kDocs;
}

}  // namespace xff::datetime
