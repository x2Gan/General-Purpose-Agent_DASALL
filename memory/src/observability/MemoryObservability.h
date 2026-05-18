#pragma once

#include <memory>
#include <string>
#include <vector>

#include "MemoryDependencies.h"

namespace dasall::memory::observability {

struct MemoryTelemetryField {
  std::string key;
  std::string value;
};

struct MemoryTelemetryContext {
  std::string request_id;
  std::string session_id;
  std::string stage;
  std::string trace_id;
  std::string profile_id;
};

class IMemoryTelemetrySink {
 public:
  virtual ~IMemoryTelemetrySink() = default;

  virtual void emit_log(
      const std::string& event_name,
      const MemoryTelemetryContext& context,
      const std::vector<MemoryTelemetryField>& fields) = 0;
  virtual void emit_metric(
      const std::string& event_name,
      const MemoryTelemetryContext& context,
      const std::vector<MemoryTelemetryField>& fields) = 0;
  virtual void emit_trace(
      const std::string& event_name,
      const MemoryTelemetryContext& context,
      const std::vector<MemoryTelemetryField>& fields) = 0;
  virtual void emit_audit(
      const std::string& event_name,
      const MemoryTelemetryContext& context,
      const std::vector<MemoryTelemetryField>& fields) = 0;
};

class MemoryObservability {
 public:
  explicit MemoryObservability(
    std::shared_ptr<IMemoryTelemetrySink> sink = nullptr,
    std::string default_profile_id = "unknown");

  void emit(
      const std::string& event_name,
      const MemoryTelemetryContext& context,
      std::vector<MemoryTelemetryField> fields = {}) const;

 private:
  std::shared_ptr<IMemoryTelemetrySink> sink_;
    std::string default_profile_id_;
};

[[nodiscard]] std::shared_ptr<IMemoryTelemetrySink> make_live_telemetry_sink(
    const MemoryRuntimeDependencies& dependencies);

}  // namespace dasall::memory::observability