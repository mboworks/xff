// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_PRESENTATION_RENDER_SUMMARY_EXPORT_H_
#define XFF_PRESENTATION_RENDER_SUMMARY_EXPORT_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "xff/presentation/render/render.h"

namespace xff::render {

struct SummaryExportBytes {
  std::uint64_t value = 0;
  std::uint64_t total = 0;
};

// Denominators describe the complete population, including groups hidden by --top.
struct SummaryExportMetrics {
  std::uint64_t count = 0;
  std::uint64_t total_count = 0;
  std::optional<SummaryExportBytes> bytes;
};

struct SummaryExportRecord {
  std::size_t request = 0;
  std::string_view summary;
  std::string_view key_template;
  std::string_view scope;
  std::string_view root;
  std::string_view left_root;
  std::string_view right_root;
  std::string_view type;
  std::string_view group;
  bool is_total = false;
  std::optional<SummaryExportMetrics> metrics;
  // Missing cells are empty, distinct from a present zero-valued cell.
  std::map<std::string, SummaryExportMetrics> scoped_metrics;
};

// One schema for the entire export, including repeated summary requests. Scope
// columns follow the resolved selector order; ordinary and comparison-result rows
// use the unprefixed metrics. Reuses the listing CSV/TSV encoding contract.
class SummaryExport {
 public:
  explicit SummaryExport(std::vector<std::string> scopes = {});

  std::string Header(Format format) const;
  std::string Row(const SummaryExportRecord& record, Format format, unsigned precision) const;

 private:
  std::vector<std::string> scopes_;
};

}  // namespace xff::render

#endif  // XFF_PRESENTATION_RENDER_SUMMARY_EXPORT_H_
