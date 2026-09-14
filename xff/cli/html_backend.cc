// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/html_backend.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

constexpr std::string_view kNavigationMarker = "<!-- xff-section-navigation -->";

bool IsUrlEnd(char chr) {
  return absl::ascii_isspace(chr) || chr == '<' || chr == '>' || chr == '"';
}

bool IsUrlStart(std::string_view text, std::size_t position) {
  if (position == 0) {
    return true;
  }
  const char before = text[position - 1];
  return absl::ascii_isspace(before) || before == '(' || before == '[' || before == '{' || before == '<';
}

std::size_t FindUrl(std::string_view text, std::size_t cursor) {
  while (cursor < text.size()) {
    const std::size_t begin = std::min(text.find("http://", cursor), text.find("https://", cursor));
    if (begin == std::string_view::npos || IsUrlStart(text, begin)) {
      return begin;
    }
    cursor = begin + 1;
  }
  return std::string_view::npos;
}

std::size_t LinkEnd(std::string_view text, std::size_t begin) {
  std::size_t end = begin;
  int parentheses_balance = 0;
  while (end < text.size() && !IsUrlEnd(text[end])) {
    if (text[end] == '(') {
      ++parentheses_balance;
    } else if (text[end] == ')') {
      --parentheses_balance;
    }
    ++end;
  }
  while (end > begin) {
    const char tail = text[end - 1];
    if (tail == '.' || tail == ',' || tail == ';' || tail == ':' || tail == '!' || tail == '?'
        || (tail == ')' && parentheses_balance < 0)) {
      if (tail == ')') {
        ++parentheses_balance;
      }
      --end;
      continue;
    }
    break;
  }
  return end;
}

std::string Tags(const Entry& entry) {
  if (!entry.tags.empty()) {
    return absl::StrCat("<span class=\"tags\">(", HtmlEscape(absl::StrJoin(entry.tags, ", ")), ")</span>");
  }
  return entry.xff ? "<span class=\"tags\">(xff)</span>" : std::string();
}

}  // namespace

std::string HtmlBackend::UniqueAnchor(std::string_view explicit_anchor, std::string_view fallback) {
  std::string anchor = HtmlSlug(explicit_anchor.empty() ? fallback : explicit_anchor);
  const std::size_t occurrence = anchor_counts_[anchor]++;
  if (occurrence != 0) {
    absl::StrAppend(&anchor, "-", occurrence);
  }
  return HtmlAttributeEscape(anchor);
}

std::string HtmlEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char chr : text) {
    switch (chr) {
      case '&': absl::StrAppend(&out, "&amp;"); break;
      case '<': absl::StrAppend(&out, "&lt;"); break;
      case '>': absl::StrAppend(&out, "&gt;"); break;
      default: out.push_back(chr); break;
    }
  }
  return out;
}

std::string HtmlAttributeEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char chr : text) {
    switch (chr) {
      case '&': absl::StrAppend(&out, "&amp;"); break;
      case '<': absl::StrAppend(&out, "&lt;"); break;
      case '>': absl::StrAppend(&out, "&gt;"); break;
      case '"': absl::StrAppend(&out, "&quot;"); break;
      case '\'': absl::StrAppend(&out, "&#39;"); break;
      default: out.push_back(chr); break;
    }
  }
  return out;
}

std::string HtmlSlug(std::string_view text) {
  std::string slug;
  bool pending_dash = false;
  for (const char chr : text) {
    if (absl::ascii_isalnum(chr)) {
      if (pending_dash && !slug.empty()) {
        slug.push_back('-');
      }
      pending_dash = false;
      slug.push_back(absl::ascii_tolower(chr));
    } else {
      pending_dash = true;
    }
  }
  if (slug.empty()) {
    slug = "item";
    for (const unsigned char chr : text) {
      absl::StrAppend(&slug, "-", absl::StrFormat("%02x", chr));
    }
  }
  return slug;
}

std::string RenderTextHtml(std::string_view text) {
  std::string out;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const std::size_t begin = FindUrl(text, cursor);
    if (begin == std::string_view::npos) {
      absl::StrAppend(&out, HtmlEscape(text.substr(cursor)));
      break;
    }
    absl::StrAppend(&out, HtmlEscape(text.substr(cursor, begin - cursor)));
    const std::size_t end = LinkEnd(text, begin);
    const std::string_view url = text.substr(begin, end - begin);
    absl::StrAppend(&out, "<a href=\"", HtmlAttributeEscape(url), "\">", HtmlEscape(url), "</a>");
    cursor = end;
  }
  return out;
}

std::string HtmlRefLink(const RefTarget& target, std::string_view label) {
  std::string text = HtmlEscape(label.empty() ? target.id : label);
  switch (target.kind) {
    case RefTarget::Kind::kUrl: return absl::StrCat("<a href=\"", HtmlAttributeEscape(target.id), "\">", text, "</a>");
    case RefTarget::Kind::kManPage: {
      const std::string locator =
          label.empty() ? absl::StrCat(target.id, "(", target.section, ")") : std::string(label);
      return absl::StrCat("<cite>", HtmlEscape(locator), "</cite>");
    }
    case RefTarget::Kind::kTopic:
      return absl::StrCat("<a href=\"#", HtmlAttributeEscape(HtmlSlug("topic-" + target.id)), "\">", text, "</a>");
    case RefTarget::Kind::kFlag:
      return absl::StrCat("<a href=\"#", HtmlAttributeEscape(HtmlSlug("flag-" + target.id)), "\">", text, "</a>");
    case RefTarget::Kind::kPrimary:
      return absl::StrCat("<a href=\"#", HtmlAttributeEscape(HtmlSlug("primary-" + target.id)), "\">", text, "</a>");
    case RefTarget::Kind::kAnchor:
      return absl::StrCat("<a href=\"#", HtmlAttributeEscape(HtmlSlug(target.id)), "\">", text, "</a>");
  }
  return text;
}

std::string RenderInlinesHtml(const Inlines& runs) {
  std::string out;
  for (const Inline& run : runs) {
    const std::string text = run.style == Inline::Style::kText ? RenderTextHtml(run.text) : HtmlEscape(run.text);
    switch (run.style) {
      case Inline::Style::kText: absl::StrAppend(&out, text); break;
      case Inline::Style::kCode: absl::StrAppend(&out, "<code>", text, "</code>"); break;
      case Inline::Style::kEmphasis: absl::StrAppend(&out, "<em>", text, "</em>"); break;
      case Inline::Style::kStrong: absl::StrAppend(&out, "<strong>", text, "</strong>"); break;
      case Inline::Style::kRef:
        absl::StrAppend(&out, run.target.has_value() ? HtmlRefLink(*run.target, run.text) : text);
        break;
    }
  }
  return out;
}

void HtmlBackend::Preamble(const Document& doc) {
  // Reserve canonical topic anchors before an earlier subsection can claim the same spelling.
  for (const Section& section : doc.sections) {
    if (!section.anchor.empty()) {
      anchor_counts_.try_emplace(HtmlSlug(section.anchor), 1);
    }
  }
  absl::StrAppend(
      &out_, "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n",
      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>", HtmlEscape(doc.name),
      " reference</title>\n<style>\n",
      ":root{color-scheme:light dark;--bg:#fff;--fg:#202124;--muted:#59636e;--rule:#c7d0da;--soft:#f6f8fa;"
      "--code:#eef1f4;--accent:#176b87;--accent-soft:#eaf5f8;--link:#0969da}",
      "@media(prefers-color-scheme:dark){:root{--bg:#0d1117;--fg:#e6edf3;--muted:#9da7b3;--rule:#3d4752;"
      "--soft:#151b23;--code:#1c2530;--accent:#67c1dc;--accent-soft:#112a33;--link:#58a6ff}}",
      "*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:var(--bg);color:var(--fg);font:"
      "16px/1.55 system-ui,-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif}",
      "main,header{width:min(100% - 2rem,72rem);margin-inline:auto}header{padding:3rem 0 1.75rem;border-bottom:2px "
      "solid var(--accent)}",
      "main{padding:1rem 0 "
      "4rem}h1,h2,h3,h4{line-height:1.25;scroll-margin-top:1rem}h1{margin:0;font-size:2.5rem}h2{margin-top:2.5rem;"
      "padding-bottom:.4rem;border-bottom:2px solid var(--rule)}",
      "h3{margin-top:2rem}h4{margin:.2rem "
      "0}.tagline,.tags{color:var(--muted)}.usage{font-size:1.05rem}.entry{padding:.65rem 0}.summary{margin:.25rem "
      "0}.tags{margin-left:.5rem;font-weight:400;font-size:.9rem}",
      "code,pre{font-family:ui-monospace,SFMono-Regular,Consolas,monospace}code{padding:.12rem "
      ".3rem;border-radius:.25rem;background:var(--code)}pre{overflow:auto;padding:1rem;border:1px solid "
      "var(--rule);border-radius:.4rem;background:var(--code)}pre code{padding:0;background:none}",
      "a{color:var(--link);text-underline-offset:.15em}a:hover{text-decoration-thickness:2px}"
      ".contents{margin:1.5rem 0 .25rem;padding:1rem 1.2rem;border:1px solid var(--rule);border-left:4px solid "
      "var(--accent);border-radius:.4rem;background:var(--soft)}.contents strong{color:var(--accent)}"
      ".contents ul{columns:3;gap:2rem;margin:.55rem 0 0;padding-left:1.2rem}.contents "
      "li{break-inside:avoid;margin:.2rem 0}"
      "dl.rows{display:grid;grid-template-columns:minmax(10rem,max-content) 1fr;gap:.55rem 1rem;padding:.85rem;"
      "border:1px solid var(--rule);border-radius:.4rem;background:var(--soft)}dt{font-weight:600}dd{margin:0}"
      ".table-wrap{max-width:100%;overflow-x:auto;margin:1rem 0;border:1px solid var(--rule);border-radius:.4rem}"
      ".table-wrap:focus{outline:2px solid var(--accent);outline-offset:2px}"
      "table{width:100%;border-collapse:collapse}th,td{padding:.55rem .75rem;border-right:1px solid var(--rule);"
      "border-bottom:1px solid "
      "var(--rule);text-align:left;vertical-align:top}th:last-child,td:last-child{border-right:0}"
      "tbody tr:last-child td{border-bottom:0}th{background:var(--accent-soft);color:var(--fg);font-weight:700}"
      "tbody tr:nth-child(even){background:var(--soft)}",
      "@media(max-width:42rem){.contents ul{columns:1}dl.rows{display:block}dd{margin:.15rem 0 .5rem}"
      ".tags{display:block;margin:.2rem 0 0}}\n",
      "</style>\n</head>\n<body>\n<header>\n<h1>", HtmlEscape(doc.name), "</h1>\n<p class=\"tagline\">",
      HtmlEscape(doc.tagline), ".</p>\n<p class=\"usage\"><strong>Usage:</strong> <code>", HtmlEscape(doc.name), " ",
      HtmlEscape(doc.usage), "</code></p>\n", kNavigationMarker, "\n</header>\n<main>\n");
}

void HtmlBackend::BeginSection(const Section& section) {
  const std::string canonical = HtmlAttributeEscape(HtmlSlug(section.anchor));
  const bool first_explicit = !section.anchor.empty() && std::ranges::none_of(section_links_, [&](const auto& link) {
    return link.second == canonical;
  });
  const std::string anchor = first_explicit ? canonical : UniqueAnchor(section.anchor, section.title);
  if (first_explicit) {
    anchor_counts_.try_emplace(HtmlSlug(section.anchor), 1);
  }
  section_links_.emplace_back(section.title, anchor);
  absl::StrAppend(&out_, "<section id=\"", anchor, "\">\n<h2>", HtmlEscape(section.title), "</h2>\n");
}

void HtmlBackend::EndSection(const Section& /*section*/) {
  absl::StrAppend(&out_, "</section>\n");
}

void HtmlBackend::BeginSubsection(const Subsection& subsection) {
  if (subsection.title.empty()) {
    absl::StrAppend(&out_, "<div class=\"group\">\n");
    return;
  }
  absl::StrAppend(
      &out_, "<section id=\"", UniqueAnchor(subsection.anchor, subsection.title), "\">\n<h3>",
      HtmlEscape(subsection.title), "</h3>\n");
}

void HtmlBackend::EndSubsection(const Subsection& subsection) {
  absl::StrAppend(&out_, subsection.title.empty() ? "</div>\n" : "</section>\n");
}

void HtmlBackend::BeginEntry(const Entry& entry) {
  absl::StrAppend(
      &out_, R"(<article class="entry" id=")", UniqueAnchor(entry.anchor, entry.term), "\">\n<h4><code>",
      HtmlEscape(entry.term), "</code>", Tags(entry), "</h4>\n<p class=\"summary\">", RenderInlinesHtml(entry.summary),
      "</p>\n");
}

void HtmlBackend::EndEntry(const Entry& /*entry*/) {
  absl::StrAppend(&out_, "</article>\n");
}

void HtmlBackend::EmitProse(const Prose& prose) {
  absl::StrAppend(&out_, "<p>", RenderInlinesHtml(prose.runs), "</p>\n");
}

void HtmlBackend::EmitExample(const Example& example) {
  const std::string language = example.lang.empty()
                                   ? std::string()
                                   : absl::StrCat(" class=\"language-", HtmlAttributeEscape(example.lang), "\"");
  absl::StrAppend(&out_, "<pre><code", language, ">", HtmlEscape(example.text), "</code></pre>\n");
}

void HtmlBackend::EmitBullets(const Bullets& bullets) {
  absl::StrAppend(&out_, "<ul>\n");
  for (const Inlines& item : bullets.items) {
    absl::StrAppend(&out_, "<li>", RenderInlinesHtml(item), "</li>\n");
  }
  absl::StrAppend(&out_, "</ul>\n");
}

void HtmlBackend::EmitRows(const Rows& rows) {
  absl::StrAppend(&out_, "<dl class=\"rows\">\n");
  for (const Row& row : rows.rows) {
    absl::StrAppend(
        &out_, "<dt><code>", HtmlEscape(row.term), "</code></dt><dd>", RenderInlinesHtml(row.description), "</dd>\n");
  }
  absl::StrAppend(&out_, "</dl>\n");
}

void HtmlBackend::EmitTable(const Table& table) {
  absl::StrAppend(
      &out_, R"(<div class="table-wrap" role="region" tabindex="0" aria-label="Table: )",
      HtmlAttributeEscape(absl::StrJoin(table.header, ", ")), "\">\n<table>\n<thead><tr>");
  for (const std::string& cell : table.header) {
    absl::StrAppend(&out_, "<th scope=\"col\">", RenderTextHtml(cell), "</th>");
  }
  absl::StrAppend(&out_, "</tr></thead>\n<tbody>\n");
  for (const std::vector<std::string>& row : table.cells) {
    absl::StrAppend(&out_, "<tr>");
    for (const std::string& cell : row) {
      absl::StrAppend(&out_, "<td>", RenderTextHtml(cell), "</td>");
    }
    absl::StrAppend(&out_, "</tr>\n");
  }
  absl::StrAppend(&out_, "</tbody>\n</table>\n</div>\n");
}

void HtmlBackend::EmitSeeAlso(const SeeAlso& see_also) {
  absl::StrAppend(&out_, "<p class=\"see-also\">");
  if (!see_also.refs.empty() && see_also.refs.front().kind != RefTarget::Kind::kManPage) {
    absl::StrAppend(&out_, "See also: ");
  }
  for (std::size_t i = 0; i < see_also.refs.size(); ++i) {
    if (i != 0) {
      absl::StrAppend(&out_, ", ");
    }
    absl::StrAppend(&out_, HtmlRefLink(see_also.refs[i], HelpReferenceLabel(see_also.refs[i])));
  }
  absl::StrAppend(&out_, "</p>\n");
  if (!see_also.note.empty()) {
    absl::StrAppend(&out_, "<p>", RenderInlinesHtml(see_also.note), "</p>\n");
  }
}

std::string HtmlBackend::Take() {
  std::string navigation =
      "<nav class=\"contents\" aria-label=\"Reference sections\">\n<strong>On this page</strong>\n<ul>\n";
  for (const auto& [title, anchor] : section_links_) {
    absl::StrAppend(
        &navigation, "<li><a href=\"#", HtmlAttributeEscape(anchor), "\">", HtmlEscape(title), "</a></li>\n");
  }
  absl::StrAppend(&navigation, "</ul>\n</nav>");
  if (const std::size_t marker = out_.find(kNavigationMarker); marker != std::string::npos) {
    out_.replace(marker, kNavigationMarker.size(), navigation);
  }
  absl::StrAppend(&out_, "</main>\n</body>\n</html>\n");
  return std::move(out_);
}

}  // namespace xff::cli
