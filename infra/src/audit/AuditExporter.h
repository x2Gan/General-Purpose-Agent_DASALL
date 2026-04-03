#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "audit/AuditExporterTypes.h"

namespace dasall::infra::audit {

class AuditExporter {
 public:
  AuditExporter(const std::vector<AuditEvent>* primary_records,
                const std::vector<AuditEvent>* fallback_records,
                std::optional<std::size_t> max_page_size = std::nullopt)
      : primary_records_(primary_records),
        fallback_records_(fallback_records),
        max_page_size_(max_page_size) {}

  [[nodiscard]] ExportResult export_records(const ExportQuery& query) const;

 private:
  [[nodiscard]] std::vector<AuditEvent> collect_matching_records(
      const ExportQuery& query) const;

  const std::vector<AuditEvent>* primary_records_ = nullptr;
  const std::vector<AuditEvent>* fallback_records_ = nullptr;
  std::optional<std::size_t> max_page_size_;
};

}  // namespace dasall::infra::audit