#include "AgentFacade.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "AgentOrchestrator.h"
#include "RuntimeDependencySet.h"
#include "audit/AuditTypes.h"
#include "audit/IAuditLogger.h"
#include "error/ResultCode.h"
#include "logging/RuntimeStructuredLogUtils.h"
#include "metrics/IMeter.h"
#include "metrics/IMetricsProvider.h"
#include "metrics/MetricTypes.h"
#include "telemetry/RuntimeEventBus.h"

namespace dasall::runtime {
    namespace {

        struct RuntimeCompositionRoot {
            std::string runtime_instance_id;
            std::string profile_id;
            std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot;
            std::shared_ptr<RuntimeDependencySet> dependency_set;
            std::unique_ptr<AgentOrchestrator> orchestrator;
            std::optional<SessionSnapshot> waiting_session;
            RuntimeDependencyReadiness readiness;
            bool degraded = false;
        };

        constexpr char kRuntimePathTagPrefix[] = "runtime_path:";
        constexpr char kRuntimePathDirectLlmTag[] = "runtime_path:direct_llm";
        constexpr char kRuntimePathCognitionFirstTag[] = "runtime_path:cognition_first";
        constexpr char kRuntimePathToolPositiveTag[] = "runtime_path:tool_positive";
        constexpr char kRuntimePathRecoveryPositiveTag[] = "runtime_path:recovery_positive";

        /**
         * @brief Gets the current time in milliseconds since the epoch.
         * 
         * @return std::int64_t 
         */
        [[nodiscard]] std::int64_t current_time_ms() {
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        }

        /**
         * @brief Converts a string to an optional string, returning std::nullopt if the string is empty.
         * 
         * @param value The string to convert.
         * @return std::optional<std::string> The optional string.
         */
        [[nodiscard]] std::optional<std::string> optional_string(const std::string& value) {
            if (value.empty()) {
                return std::nullopt;
            }

            return value;
        }

        /**
         * @brief Gets the runtime instance ID attribute from the runtime composition root.
         * 
         * @param root The runtime composition root.
         * @return std::optional<std::string> The runtime instance ID attribute.
         */
        [[nodiscard]] std::optional<std::string>
        runtime_instance_id_attr(const RuntimeCompositionRoot& root) {
            return optional_string(root.runtime_instance_id);
        }

        /**
         * @brief Gets the logger from the runtime composition root.
         * 
         * @param root The runtime composition root.
         * @return std::shared_ptr<infra::logging::ILogger> The logger.
         */
        [[nodiscard]] std::shared_ptr<infra::logging::ILogger>
        logger_from_root(const RuntimeCompositionRoot& root) {
            if (root.dependency_set == nullptr) {
                return nullptr;
            }

            return root.dependency_set->logger;
        }

        /**
         * @brief Gets the logger from the agent initialization request.
         * 
         * @param request The agent initialization request.
         * @return std::shared_ptr<infra::logging::ILogger> The logger.
         */
        [[nodiscard]] std::shared_ptr<infra::logging::ILogger>
        logger_from_request(const AgentInitRequest& request) {
            if (request.dependency_set == nullptr) {
                return nullptr;
            }

            return request.dependency_set->logger;
        }

        /**
         * @brief Gets the name of the agent result status.
         * 
         * @param status The agent result status.
         * @return const char* The name of the agent result status.
         */
        [[nodiscard]] const char*
        agent_result_status_name(const std::optional<contracts::AgentResultStatus>& status) {
            if (!status.has_value()) {
                return "Unspecified";
            }

            switch (*status) {
            case contracts::AgentResultStatus::Unspecified:
                return "Unspecified";
            case contracts::AgentResultStatus::Completed:
                return "Completed";
            case contracts::AgentResultStatus::Failed:
                return "Failed";
            case contracts::AgentResultStatus::PartiallyCompleted:
                return "PartiallyCompleted";
            case contracts::AgentResultStatus::Cancelled:
                return "Cancelled";
            case contracts::AgentResultStatus::Timeout:
                return "Timeout";
            }

            return "Unknown";
        }

        /**
         * @brief Gets the log level for the facade initialization result.
         * 
         * @param result The agent initialization result.
         * @return infra::LogLevel The log level.
         */
        [[nodiscard]] infra::LogLevel facade_init_log_level(const AgentInitResult& result) {
            return result.accepted ? infra::LogLevel::Info : infra::LogLevel::Error;
        }

        /**
         * @brief Gets the log level for the facade result.
         * 
         * @param result The agent result.
         * @return infra::LogLevel The log level.
         */
        [[nodiscard]] infra::LogLevel
        facade_result_log_level(const contracts::AgentResult& result) {
            if (!result.status.has_value()) {
                return infra::LogLevel::Warn;
            }

            switch (*result.status) {
            case contracts::AgentResultStatus::Completed:
                return infra::LogLevel::Info;
            case contracts::AgentResultStatus::PartiallyCompleted:
            case contracts::AgentResultStatus::Cancelled:
                return infra::LogLevel::Warn;
            case contracts::AgentResultStatus::Failed:
            case contracts::AgentResultStatus::Timeout:
                return infra::LogLevel::Error;
            case contracts::AgentResultStatus::Unspecified:
                return infra::LogLevel::Warn;
            }

            return infra::LogLevel::Warn;
        }

        /**
         * @brief Finds the runtime path tag in the agent result.
         * 
         * @param result The agent result.
         * @return std::optional<std::string> The runtime path tag if found, otherwise std::nullopt.
         */
        [[nodiscard]] std::optional<std::string>
        find_runtime_path_tag(const contracts::AgentResult& result) {
            if (!result.tags.has_value()) {
                return std::nullopt;
            }

            const auto tag_it =
                std::find_if(result.tags->begin(), result.tags->end(), [](const std::string& tag) {
                    return tag.rfind(kRuntimePathTagPrefix, 0) == 0;
                });
            if (tag_it == result.tags->end()) {
                return std::nullopt;
            }

            return *tag_it;
        }

        void emit_facade_init_log(const std::shared_ptr<infra::logging::ILogger>& logger,
                                  const AgentInitRequest& request, const AgentInitResult& result) {
            infra::LogEvent::AttributeMap attrs;
            detail::add_string_attr(attrs, "profile_id", request.profile_id);
            detail::add_string_attr(attrs, "resolved_profile_id", result.resolved_profile_id);
            detail::add_bool_attr(attrs, "accepted", result.accepted);
            detail::add_bool_attr(attrs, "degraded", result.degraded);
            detail::add_bool_attr(attrs, "cold_start", request.cold_start);
            detail::add_string_attr(attrs, "readiness_level", result.readiness_label());
            detail::add_integer_attr(attrs, "missing_required_port_count",
                                     result.missing_required_ports.size());
            detail::add_integer_attr(attrs, "missing_optional_port_count",
                                     result.missing_optional_ports.size());
            detail::add_integer_attr(attrs, "degraded_reason_count",
                                     result.degraded_reasons.size());
            if (result.error_code != 0) {
                detail::add_integer_attr(attrs, "error_code", result.error_code);
            }
            detail::add_string_attr(attrs, "detail",
                                    !result.diagnostics.empty() ? result.diagnostics
                                                                : result.health_summary);
            detail::emit_runtime_log(logger, facade_init_log_level(result), "runtime.facade.init",
                                     "agent_facade", optional_string(result.runtime_instance_id),
                                     std::move(attrs));
        }

        void emit_facade_result_log(const std::shared_ptr<infra::logging::ILogger>& logger,
                                    const std::optional<std::string>& runtime_instance_id,
                                    const std::string_view event_name,
                                    const std::optional<std::string>& request_id,
                                    const std::optional<std::string>& session_id,
                                    const std::optional<std::string>& trace_id,
                                    const std::optional<std::string>& checkpoint_ref,
                                    const contracts::AgentResult& result,
                                    const std::string_view outcome_reason,
                                    const bool waiting_session_active) {
            infra::LogEvent::AttributeMap attrs;
            detail::add_optional_string_attr(attrs, "request_id", request_id);
            detail::add_optional_string_attr(attrs, "session_id", session_id);
            detail::add_optional_string_attr(attrs, "trace_id", trace_id);
            detail::add_optional_string_attr(attrs, "checkpoint_ref", checkpoint_ref);
            detail::add_optional_string_attr(attrs, "result_id", result.result_id);
            detail::add_string_attr(attrs, "result_status",
                                    agent_result_status_name(result.status));
            if (result.result_code.has_value()) {
                detail::add_integer_attr(attrs, "result_code", *result.result_code);
            }
            detail::add_bool_attr(attrs, "task_completed", result.task_completed.value_or(false));
            detail::add_bool_attr(attrs, "waiting_session_active", waiting_session_active);
            detail::add_optional_string_attr(attrs, "runtime_path_tag",
                                             find_runtime_path_tag(result));
            if (result.tags.has_value()) {
                detail::add_integer_attr(attrs, "tag_count", result.tags->size());
            }
            detail::add_string_attr(attrs, "outcome_reason", outcome_reason);
            detail::emit_runtime_log(logger, facade_result_log_level(result), event_name,
                                     "agent_facade", runtime_instance_id, std::move(attrs));
        }

        void emit_facade_stop_log(const std::shared_ptr<infra::logging::ILogger>& logger,
                                  const std::optional<std::string>& runtime_instance_id,
                                  const bool was_initialized, const std::uint32_t timeout_ms) {
            infra::LogEvent::AttributeMap attrs;
            detail::add_bool_attr(attrs, "was_initialized", was_initialized);
            detail::add_bool_attr(attrs, "stopped", true);
            detail::add_integer_attr(attrs, "timeout_ms", timeout_ms);
            detail::emit_runtime_log(logger, infra::LogLevel::Info, "runtime.facade.stop",
                                     "agent_facade", runtime_instance_id, std::move(attrs));
        }

        void append_diagnostic_fragment(std::string& diagnostics, const std::string& fragment) {
            if (fragment.empty()) {
                return;
            }

            if (!diagnostics.empty()) {
                diagnostics += ";";
            }
            diagnostics += fragment;
        }

        void append_unique_tag(std::vector<std::string>& tags, const std::string& tag) {
            if (tag.empty()) {
                return;
            }

            if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
                tags.push_back(tag);
            }
        }

        void append_unique_value(std::vector<std::string>& values, const std::string& value) {
            if (value.empty()) {
                return;
            }

            if (std::find(values.begin(), values.end(), value) == values.end()) {
                values.push_back(value);
            }
        }

        [[nodiscard]] std::string join_values(const std::vector<std::string>& values) {
            std::string joined;
            for (std::size_t index = 0; index < values.size(); ++index) {
                if (index != 0U) {
                    joined += ",";
                }
                joined += values[index];
            }
            return joined;
        }

        class RuntimeAuditSinkSubscriber;
        class RuntimeMetricsSinkSubscriber;

        struct RuntimeSinkAttachment {
            std::vector<RuntimeEventSubscription> subscriptions;
            std::shared_ptr<RuntimeAuditSinkSubscriber> audit_subscriber;
            std::shared_ptr<RuntimeMetricsSinkSubscriber> metrics_subscriber;
        };

        constexpr std::array<std::string_view, 10> kRuntimeSinkEventFilters{
            "runtime.recovery.reject",
            "recovery_reject",
            "runtime.safe_mode",
            "safe_mode_entered",
            "runtime.budget.reject",
            "budget_reject",
            "runtime.checkpoint.save_failure",
            "checkpoint_save_failure",
            "runtime.high_risk.confirmation",
            "high_risk_confirmation",
        };

        [[nodiscard]] std::string runtime_sink_event_stage(const std::string_view event_name) {
            if (event_name == "runtime.recovery.reject" || event_name == "recovery_reject") {
                return "recovery_reject";
            }
            if (event_name == "runtime.safe_mode" || event_name == "safe_mode_entered") {
                return "safe_mode_entered";
            }
            if (event_name == "runtime.budget.reject" || event_name == "budget_reject") {
                return "budget_reject";
            }
            if (event_name == "runtime.checkpoint.save_failure" ||
                event_name == "checkpoint_save_failure") {
                return "checkpoint_save_failure";
            }
            if (event_name == "runtime.high_risk.confirmation" ||
                event_name == "high_risk_confirmation") {
                return "high_risk_confirmation";
            }
            return std::string(event_name);
        }

        [[nodiscard]] infra::AuditOutcome runtime_sink_audit_outcome(
            const std::string_view event_name) {
            const auto stage = runtime_sink_event_stage(event_name);
            if (stage == "high_risk_confirmation") {
                return infra::AuditOutcome::Succeeded;
            }
            if (stage == "checkpoint_save_failure") {
                return infra::AuditOutcome::Failed;
            }
            if (stage == "safe_mode_entered") {
                return infra::AuditOutcome::Escalated;
            }
            return infra::AuditOutcome::Rejected;
        }

        [[nodiscard]] infra::AuditEvidenceKind runtime_sink_evidence_kind(
            const std::string_view event_name) {
            const auto stage = runtime_sink_event_stage(event_name);
            if (stage == "recovery_reject" || stage == "checkpoint_save_failure") {
                return infra::AuditEvidenceKind::RecoveryOutcome;
            }
            return infra::AuditEvidenceKind::WorkerTask;
        }

        [[nodiscard]] std::string metric_outcome_label(const RuntimeEventEnvelope& event) {
            const auto stage = runtime_sink_event_stage(event.event_name);
            if (stage == "high_risk_confirmation") {
                return "success";
            }
            if (stage == "safe_mode_entered" && event.severity == RuntimeEventSeverity::Warning) {
                return "degraded";
            }
            return "failure";
        }

        [[nodiscard]] std::optional<std::string>
        find_runtime_event_attribute(const RuntimeEventEnvelope& event, const std::string_view key) {
            const auto attribute_it = std::find_if(
                event.attributes.begin(), event.attributes.end(),
                [&key](const RuntimeEventAttribute& attribute) { return attribute.key == key; });
            if (attribute_it == event.attributes.end() || attribute_it->value.empty()) {
                return std::nullopt;
            }

            return attribute_it->value;
        }

        [[nodiscard]] std::string runtime_sink_reference(const RuntimeEventEnvelope& event) {
            const auto checkpoint_ref = find_runtime_event_attribute(event, "checkpoint_ref");
            if (checkpoint_ref.has_value()) {
                return *checkpoint_ref;
            }
            if (event.context.checkpoint_id.has_value() && !event.context.checkpoint_id->empty()) {
                return *event.context.checkpoint_id;
            }
            if (event.context.turn_id.has_value() && !event.context.turn_id->empty()) {
                return *event.context.turn_id;
            }
            if (event.context.request_id.has_value() && !event.context.request_id->empty()) {
                return *event.context.request_id;
            }

            return event.event_name + "#" + std::to_string(event.sequence);
        }

        [[nodiscard]] std::string audit_context_component(
            const std::optional<std::string>& value) {
            return value.has_value() && !value->empty() ? *value
                                                        : std::string(infra::kAuditContextUnknown);
        }

        [[nodiscard]] std::string runtime_error_code_label(
            const std::optional<RuntimeErrorCode>& error_code) {
            return error_code.has_value() ? std::to_string(static_cast<int>(*error_code))
                                          : std::string();
        }

        class RuntimeAuditSinkSubscriber {
          public:
            RuntimeAuditSinkSubscriber(std::shared_ptr<infra::audit::IAuditLogger> audit_logger,
                                       std::string runtime_instance_id)
                : audit_logger_(std::move(audit_logger)),
                  runtime_instance_id_(std::move(runtime_instance_id)) {}

            void handle(const RuntimeEventEnvelope& event,
                        const profiles::RuntimeSinkPolicy& sink_policy) const {
                if (audit_logger_ == nullptr) {
                    if (sink_policy.fail_closed_on_audit_failure) {
                        throw std::runtime_error("runtime audit sink missing for event " +
                                                 std::string(event.event_name));
                    }
                    return;
                }

                infra::AuditEvent audit_event;
                audit_event.event_id = "runtime::" + event.event_name + "#" +
                                       std::to_string(event.sequence);
                audit_event.action = runtime_sink_event_stage(event.event_name);
                audit_event.actor = runtime_instance_id_.empty() ? std::string("runtime")
                                                                 : runtime_instance_id_;
                audit_event.target = audit_event.actor;
                audit_event.outcome = runtime_sink_audit_outcome(event.event_name);
                audit_event.evidence_ref = infra::AuditEvidenceRef{
                    .kind = runtime_sink_evidence_kind(event.event_name),
                    .ref = runtime_sink_reference(event),
                };
                if (event.error_code.has_value()) {
                    audit_event.side_effects.push_back("runtime_error_code=" +
                                                       runtime_error_code_label(event.error_code));
                }
                audit_event.timestamp = event.timestamp_ms > 0 ? event.timestamp_ms
                                                               : current_time_ms();

                const auto write_outcome = audit_logger_->write_audit(
                    audit_event,
                    infra::AuditContext{
                        .request_id = audit_context_component(event.context.request_id),
                        .session_id = audit_context_component(event.context.session_id),
                        .trace_id = audit_context_component(event.context.trace_id),
                        .task_id = audit_context_component(event.context.turn_id),
                        .parent_task_id = audit_context_component(event.context.checkpoint_id),
                        .lease_id = runtime_instance_id_.empty()
                                        ? std::string(infra::kAuditContextUnknown)
                                        : runtime_instance_id_,
                        .worker_type = std::string("runtime_control_plane"),
                    });
                if (!write_outcome.is_success() && !write_outcome.is_degraded_success() &&
                    sink_policy.fail_closed_on_audit_failure) {
                    throw std::runtime_error("runtime audit sink rejected event " +
                                             std::string(event.event_name));
                }
            }

          private:
            std::shared_ptr<infra::audit::IAuditLogger> audit_logger_;
            std::string runtime_instance_id_;
        };

        class RuntimeMetricsSinkSubscriber {
          public:
            RuntimeMetricsSinkSubscriber(
                std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider,
                std::string runtime_instance_id, std::string profile_id)
                : metrics_provider_(std::move(metrics_provider)),
                  runtime_instance_id_(std::move(runtime_instance_id)),
                  profile_id_(std::move(profile_id)) {}

            void handle(const RuntimeEventEnvelope& event,
                        const profiles::RuntimeSinkPolicy& sink_policy) {
                auto meter = ensure_meter(sink_policy, event.event_name);
                if (meter == nullptr) {
                    return;
                }

                const auto status = meter->record(infra::metrics::MetricSample{
                    .identity_ref = metric_identity(),
                    .value = 1.0,
                    .ts_unix_ms = event.timestamp_ms > 0 ? event.timestamp_ms : current_time_ms(),
                    .labels = infra::metrics::MetricLabels{
                        .module = "runtime",
                        .stage = runtime_sink_event_stage(event.event_name),
                        .profile = profile_id_.empty() ? std::string("runtime") : profile_id_,
                        .outcome = metric_outcome_label(event),
                        .error_code = runtime_error_code_label(event.error_code),
                    },
                });
                if (!status.ok && !sink_policy.drop_on_metrics_failure) {
                    throw std::runtime_error("runtime metrics sink failed for event " +
                                             std::string(event.event_name));
                }
            }

          private:
            [[nodiscard]] static infra::metrics::MetricIdentity metric_identity() {
                return infra::metrics::MetricIdentity{
                    .name = "runtime_control_plane_event_total",
                    .type = infra::metrics::MetricType::Counter,
                    .unit = "1",
                    .description = "Critical runtime control-plane events forwarded to metrics",
                };
            }

            [[nodiscard]] std::shared_ptr<infra::metrics::IMeter>
            ensure_meter(const profiles::RuntimeSinkPolicy& sink_policy,
                         const std::string_view event_name) {
                std::lock_guard<std::mutex> lock(meter_mutex_);
                if (metrics_provider_ == nullptr) {
                    if (!sink_policy.drop_on_metrics_failure) {
                        throw std::runtime_error("runtime metrics sink missing for event " +
                                                 std::string(event_name));
                    }
                    return nullptr;
                }

                if (meter_ == nullptr) {
                    meter_ = metrics_provider_->get_meter(infra::metrics::MeterScope{
                        .name = "runtime.control_plane",
                        .version = "v1",
                        .schema_url = "",
                    });
                    if (meter_ == nullptr) {
                        if (!sink_policy.drop_on_metrics_failure) {
                            throw std::runtime_error("runtime metrics sink could not acquire meter for event " +
                                                     std::string(event_name));
                        }
                        return nullptr;
                    }

                    counter_handle_ = meter_->create_counter(metric_identity());
                    if (!counter_handle_.has_value()) {
                        if (!sink_policy.drop_on_metrics_failure) {
                            throw std::runtime_error("runtime metrics sink could not register counter for event " +
                                                     std::string(event_name));
                        }
                        meter_.reset();
                        return nullptr;
                    }
                }

                return meter_;
            }

            std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider_;
            std::string runtime_instance_id_;
            std::string profile_id_;
            std::mutex meter_mutex_;
            std::shared_ptr<infra::metrics::IMeter> meter_;
            std::optional<infra::metrics::InstrumentHandle> counter_handle_;
        };

        void apply_runtime_sink_policy_readiness(
            const profiles::RuntimePolicySnapshot& snapshot,
            const RuntimeDependencySet& dependency_set, const bool runtime_local_stub_path,
            RuntimeDependencyReadiness& readiness) {
            if (runtime_local_stub_path) {
                return;
            }

            const auto& sink_policy = snapshot.runtime_sink_policy();
            const bool audit_required = sink_policy.fail_closed_on_audit_failure;
            const bool metrics_required = !sink_policy.drop_on_metrics_failure;
            const bool any_sink_expected = audit_required || metrics_required ||
                                           dependency_set.audit_logger != nullptr ||
                                           dependency_set.metrics_provider != nullptr;

            if (dependency_set.audit_logger == nullptr) {
                append_unique_value(audit_required ? readiness.missing_required_ports
                                                  : readiness.missing_optional_ports,
                                    "audit");
            }
            if (dependency_set.metrics_provider == nullptr) {
                append_unique_value(metrics_required ? readiness.missing_required_ports
                                                    : readiness.missing_optional_ports,
                                    "metrics");
            }
            if (dependency_set.runtime_event_bus == nullptr && any_sink_expected) {
                append_unique_value((audit_required || metrics_required)
                                        ? readiness.missing_required_ports
                                        : readiness.missing_optional_ports,
                                    "runtime_event_bus");
            }

            readiness.has_required_ports = readiness.missing_required_ports.empty();
            readiness.has_optional_ports = readiness.missing_optional_ports.empty();
            readiness.degraded = readiness.has_required_ports && !readiness.has_optional_ports;
        }

        bool install_runtime_sink_subscribers(const AgentInitRequest& request,
                                              const std::string& resolved_profile_id,
                                              const bool runtime_local_stub_path,
                                              AgentInitResult& result,
                                              RuntimeSinkAttachment& attachment) {
            attachment = RuntimeSinkAttachment{};
            if (runtime_local_stub_path || request.dependency_set == nullptr ||
                request.policy_snapshot == nullptr) {
                return true;
            }

            const auto event_bus = request.dependency_set->runtime_event_bus;
            if (event_bus == nullptr) {
                append_diagnostic_fragment(result.diagnostics,
                                           "runtime_sink_subscribers=skipped_no_event_bus");
                return true;
            }

            const auto& sink_policy = request.policy_snapshot->runtime_sink_policy();
            append_diagnostic_fragment(
                result.diagnostics,
                std::string("runtime_sink_policy=") +
                    (sink_policy.fail_closed_on_audit_failure ? "audit_fail_closed"
                                                              : "audit_best_effort") +
                    "," +
                    (sink_policy.drop_on_metrics_failure ? "metrics_drop"
                                                         : "metrics_fail_closed"));

            std::vector<std::string> installed_sinks;
            if (request.dependency_set->audit_logger != nullptr) {
                attachment.audit_subscriber = std::make_shared<RuntimeAuditSinkSubscriber>(
                    request.dependency_set->audit_logger, request.runtime_instance_id);
                for (const auto event_filter : kRuntimeSinkEventFilters) {
                    attachment.subscriptions.push_back(event_bus->subscribe(
                        std::string(event_filter),
                        [subscriber = attachment.audit_subscriber,
                         sink_policy](const RuntimeEventEnvelope& event) {
                            subscriber->handle(event, sink_policy);
                        },
                        true));
                }
                installed_sinks.push_back("audit");
            }

            if (request.dependency_set->metrics_provider != nullptr) {
                attachment.metrics_subscriber =
                    std::make_shared<RuntimeMetricsSinkSubscriber>(
                        request.dependency_set->metrics_provider,
                        request.runtime_instance_id,
                        resolved_profile_id);
                for (const auto event_filter : kRuntimeSinkEventFilters) {
                    attachment.subscriptions.push_back(event_bus->subscribe(
                        std::string(event_filter),
                        [subscriber = attachment.metrics_subscriber,
                         sink_policy](const RuntimeEventEnvelope& event) {
                            subscriber->handle(event, sink_policy);
                        },
                        true));
                }
                installed_sinks.push_back("metrics");
            }

            append_diagnostic_fragment(
                result.diagnostics,
                std::string("runtime_sink_subscribers=") +
                    (installed_sinks.empty() ? std::string("none") : join_values(installed_sinks)));
            return true;
        }

        void unsubscribe_runtime_sink_subscribers(const RuntimeCompositionRoot& root,
                                                  RuntimeSinkAttachment& attachment) {
            if (root.dependency_set == nullptr || root.dependency_set->runtime_event_bus == nullptr) {
                attachment = RuntimeSinkAttachment{};
                return;
            }

            for (const auto& subscription : attachment.subscriptions) {
                if (subscription.is_valid()) {
                    static_cast<void>(root.dependency_set->runtime_event_bus->unsubscribe(
                        subscription.subscription_id));
                }
            }
            attachment = RuntimeSinkAttachment{};
        }

        [[nodiscard]] bool contains_evidence_fragment(const std::vector<std::string>& evidence,
                                                      const std::string& fragment) {
            return std::any_of(evidence.begin(), evidence.end(),
                               [&fragment](const std::string& value) {
                                   return value.find(fragment) != std::string::npos;
                               });
        }

        void clear_runtime_path_tags(std::vector<std::string>& tags) {
            tags.erase(std::remove_if(tags.begin(), tags.end(),
                                      [](const std::string& tag) {
                                          return tag.rfind(kRuntimePathTagPrefix, 0) == 0;
                                      }),
                       tags.end());
        }

        [[nodiscard]] bool result_is_path_tag_eligible(const contracts::AgentResult& result) {
            return result.status == contracts::AgentResultStatus::Completed ||
                   result.status == contracts::AgentResultStatus::PartiallyCompleted;
        }

        [[nodiscard]] std::optional<std::string>
        classify_runtime_path_tag(const RuntimeCompositionRoot& root,
                                  const OrchestratorRunResult& run_result) {
            if (root.dependency_set == nullptr ||
                !result_is_path_tag_eligible(run_result.agent_result)) {
                return std::nullopt;
            }

            if (run_result.used_recovery_round) {
                return std::string{kRuntimePathRecoveryPositiveTag};
            }

            if (run_result.used_tool_round) {
                return std::string{kRuntimePathToolPositiveTag};
            }

            const auto& evidence = root.dependency_set->external_evidence;
            if (contains_evidence_fragment(evidence, "required-live-baseline")) {
                return std::string{kRuntimePathDirectLlmTag};
            }

            if (contains_evidence_fragment(evidence, "cognition-first-forced") ||
                root.dependency_set->llm_manager != nullptr) {
                return std::string{kRuntimePathCognitionFirstTag};
            }

            return std::nullopt;
        }

        void apply_runtime_path_tag(const RuntimeCompositionRoot& root,
                                    OrchestratorRunResult& run_result) {
            const auto path_tag = classify_runtime_path_tag(root, run_result);
            if (!path_tag.has_value()) {
                return;
            }

            if (!run_result.agent_result.tags.has_value()) {
                run_result.agent_result.tags = std::vector<std::string>{};
            }

            auto& tags = *run_result.agent_result.tags;
            clear_runtime_path_tags(tags);
            append_unique_tag(tags, *path_tag);
        }

        [[nodiscard]] bool degraded_ready_allowed(const profiles::RuntimePolicySnapshot& snapshot) {
            const auto& degrade_policy = snapshot.degrade_policy();
            return degrade_policy.allow_model_failover || degrade_policy.allow_budget_degrade;
        }

        [[nodiscard]] std::vector<std::string>
        make_init_degraded_reasons(const RuntimeDependencyReadiness& readiness) {
            std::vector<std::string> reasons;
            if (!readiness.degraded) {
                return reasons;
            }

            append_unique_value(reasons, "runtime_optional_port_gap");
            for (const auto& port : readiness.missing_optional_ports) {
                append_unique_value(reasons, std::string{"runtime_missing_optional:"} + port);
            }

            return reasons;
        }

        [[nodiscard]] bool uses_runtime_local_stub_path(const std::string& diagnostics) {
            return diagnostics.find("cognition_ports=stub_runtime_path") != std::string::npos;
        }

        void apply_runtime_readiness_tags(const RuntimeDependencyReadiness& readiness,
                                          contracts::AgentResult& result) {
            if (!readiness.degraded) {
                return;
            }

            if (!result.tags.has_value()) {
                result.tags = std::vector<std::string>{};
            }

            append_unique_tag(*result.tags, "runtime_readiness:degraded");
            append_unique_tag(*result.tags, "runtime_degraded_reason:optional_port_gap");
            for (const auto& port : readiness.missing_optional_ports) {
                append_unique_tag(*result.tags, std::string{"runtime_missing_optional:"} + port);
                if (port == "knowledge") {
                    append_unique_tag(*result.tags, "knowledge_unavailable");
                } else if (port == "llm") {
                    append_unique_tag(*result.tags, "llm_unavailable");
                } else {
                    append_unique_tag(*result.tags, port + "_unavailable");
                }
            }
        }

        [[nodiscard]] contracts::AgentResult
        make_failed_result(const std::optional<std::string>& request_id,
                           const std::optional<std::string>& trace_id, std::string message) {
            contracts::AgentResult result;
            result.result_id = std::string{"rt-fail-closed-"} + std::to_string(current_time_ms());
            result.status = contracts::AgentResultStatus::Failed;
            result.result_code =
                static_cast<std::int32_t>(contracts::ResultCode::RuntimeRetryExhausted);
            result.response_text = std::move(message);
            result.task_completed = false;
            result.created_at = current_time_ms();
            result.request_id = request_id;
            result.trace_id = trace_id;
            return result;
        }

        [[nodiscard]] StubMainLoopExit
        to_stub_main_loop_exit(const RuntimeStubMainLoopExit main_loop_exit) {
            switch (main_loop_exit) {
            case RuntimeStubMainLoopExit::DirectResponse:
                return StubMainLoopExit::DirectResponse;
            case RuntimeStubMainLoopExit::ToolRound:
                return StubMainLoopExit::ToolRound;
            case RuntimeStubMainLoopExit::WaitingClarify:
                return StubMainLoopExit::WaitingClarify;
            }

            return StubMainLoopExit::DirectResponse;
        }

        [[nodiscard]] StubRecoveryExit
        to_stub_recovery_exit(const RuntimeStubRecoveryExit recovery_exit) {
            switch (recovery_exit) {
            case RuntimeStubRecoveryExit::ContinueResponse:
                return StubRecoveryExit::ContinueResponse;
            case RuntimeStubRecoveryExit::AbortSafe:
                return StubRecoveryExit::AbortSafe;
            }

            return StubRecoveryExit::ContinueResponse;
        }

        [[nodiscard]] OrchestratorStubPorts
        make_orchestrator_stub_ports(const RuntimeDependencySet& dependency_set) {
            return OrchestratorStubPorts{
                .reject_preflight = dependency_set.local_stub_ports.reject_preflight,
                .main_loop_exit =
                    to_stub_main_loop_exit(dependency_set.local_stub_ports.main_loop_exit),
                .recovery_exit =
                    to_stub_recovery_exit(dependency_set.local_stub_ports.recovery_exit),
                .success_response_text = dependency_set.local_stub_ports.success_response_text,
                .fail_safe_response_text = dependency_set.local_stub_ports.fail_safe_response_text,
                .waiting_response_text = dependency_set.local_stub_ports.waiting_response_text,
            };
        }

        [[nodiscard]] bool is_active_waiting_session(const SessionSnapshot& session_snapshot) {
            return session_snapshot.has_active_checkpoint() &&
                   session_snapshot.pending_interaction.has_value() &&
                   session_snapshot.pending_interaction->active();
        }

        [[nodiscard]] bool compose_cognition_ports_if_needed(const AgentInitRequest& request,
                                                             AgentInitResult& result) {
            if (request.dependency_set == nullptr || request.policy_snapshot == nullptr) {
                return false;
            }

            const auto needs_cognition_engine = request.dependency_set->cognition_engine == nullptr;
            const auto needs_response_builder = request.dependency_set->response_builder == nullptr;
            if (!needs_cognition_engine && !needs_response_builder) {
                return true;
            }

            const auto requires_policy_projected_cognition_ports =
                request.dependency_set->memory_manager != nullptr ||
                request.dependency_set->tool_manager != nullptr ||
                request.dependency_set->llm_manager != nullptr ||
                request.dependency_set->knowledge_service != nullptr;
            if (!requires_policy_projected_cognition_ports) {
                result.diagnostics = "cognition_ports=stub_runtime_path";
                return true;
            }

            if (needs_cognition_engine) {
                auto cognition_engine = cognition::create_cognition_engine(
                    *request.policy_snapshot,
                    cognition::CognitionRuntimeDependencies{
                        .llm_manager = request.dependency_set->llm_manager,
                        .policy_snapshot = request.policy_snapshot,
                        .logger = request.dependency_set->logger,
                        .audit_logger = request.dependency_set->audit_logger,
                        .metrics_provider = request.dependency_set->metrics_provider,
                        .tracer_provider = request.dependency_set->tracer_provider,
                    });
                if (!cognition_engine) {
                    result.health_summary = "runtime facade rejected policy snapshot during "
                                            "cognition engine composition";
                    result.error_code =
                        static_cast<std::int32_t>(contracts::ResultCode::PolicyDenied);
                    result.diagnostics =
                        "cognition_engine composition failed: runtime policy snapshot missing "
                        "canonical stage routes or profile projection";
                    return false;
                }
                request.dependency_set->cognition_engine =
                    std::shared_ptr<cognition::ICognitionEngine>(cognition_engine.release());
            }

            if (needs_response_builder) {
                auto response_builder = cognition::create_response_builder(
                    *request.policy_snapshot,
                    cognition::CognitionRuntimeDependencies{
                        .llm_manager = request.dependency_set->llm_manager,
                        .policy_snapshot = request.policy_snapshot,
                        .logger = request.dependency_set->logger,
                        .audit_logger = request.dependency_set->audit_logger,
                        .metrics_provider = request.dependency_set->metrics_provider,
                        .tracer_provider = request.dependency_set->tracer_provider,
                    });
                if (!response_builder) {
                    result.health_summary = "runtime facade rejected policy snapshot during "
                                            "response builder composition";
                    result.error_code =
                        static_cast<std::int32_t>(contracts::ResultCode::PolicyDenied);
                    result.diagnostics =
                        "response_builder composition failed: runtime policy snapshot missing "
                        "canonical stage routes or profile projection";
                    return false;
                }
                request.dependency_set->response_builder =
                    std::shared_ptr<cognition::IResponseBuilder>(response_builder.release());
            }

            result.diagnostics = "cognition_ports=composed_from_policy_snapshot";
            return true;
        }

    } // namespace

    class AgentFacade::State {
      public:
        AgentInitResult init(const AgentInitRequest& request) {
            AgentInitResult result;
            result.runtime_instance_id = request.runtime_instance_id;
            result.resolved_profile_id = request.profile_id;
            const auto init_logger = logger_from_request(request);

            if (!request.has_minimum_requirements()) {
                result.health_summary = "runtime facade skeleton rejected incomplete init request";
                result.error_code =
                    static_cast<std::int32_t>(contracts::ResultCode::RuntimeRetryExhausted);
                result.diagnostics = "runtime_instance_id, profile_id, policy_snapshot and "
                                     "dependency_set are required";
                emit_facade_init_log(init_logger, request, result);
                return result;
            }

            if (!request.policy_snapshot->effective_profile_id().empty()) {
                result.resolved_profile_id = request.policy_snapshot->effective_profile_id();
            }

            if (!compose_cognition_ports_if_needed(request, result)) {
                emit_facade_init_log(init_logger, request, result);
                return result;
            }

            const bool runtime_local_stub_path = uses_runtime_local_stub_path(result.diagnostics);
            auto readiness = request.dependency_set->describe_readiness();
            apply_runtime_sink_policy_readiness(*request.policy_snapshot, *request.dependency_set,
                                                runtime_local_stub_path, readiness);
            result.missing_required_ports = readiness.missing_required_ports;
            result.missing_optional_ports = readiness.missing_optional_ports;
            result.degraded_reasons = make_init_degraded_reasons(readiness);
            if (runtime_local_stub_path) {
                readiness.has_required_ports = true;
                readiness.has_optional_ports = true;
                readiness.degraded = false;
                readiness.missing_required_ports.clear();
                readiness.missing_optional_ports.clear();
                result.missing_required_ports.clear();
                result.missing_optional_ports.clear();
                result.degraded_reasons.clear();
                append_diagnostic_fragment(result.diagnostics, "readiness=stub_runtime_path");
            } else {
                append_diagnostic_fragment(result.diagnostics,
                                           std::string{"readiness="} + readiness.summary());
                if (!result.degraded_reasons.empty()) {
                    append_diagnostic_fragment(result.diagnostics,
                                               std::string{"degraded_reasons="} +
                                                   join_values(result.degraded_reasons));
                }
            }
            if (!readiness.has_required_ports) {
                result.health_summary = "runtime facade rejected missing required dependency ports";
                result.error_code =
                    static_cast<std::int32_t>(contracts::ResultCode::RuntimeRetryExhausted);
                emit_facade_init_log(init_logger, request, result);
                return result;
            }

            if (readiness.degraded && !degraded_ready_allowed(*request.policy_snapshot)) {
                result.health_summary =
                    "runtime facade rejected degraded optional-port init under current profile";
                result.error_code = static_cast<std::int32_t>(contracts::ResultCode::PolicyDenied);
                emit_facade_init_log(init_logger, request, result);
                return result;
            }

            auto orchestrator = std::make_unique<AgentOrchestrator>(OrchestratorComposition{
                .runtime_instance_id = request.runtime_instance_id,
                .profile_id = result.resolved_profile_id,
                .policy_snapshot = request.policy_snapshot,
                .dependency_set = request.dependency_set,
                .stub_ports = make_orchestrator_stub_ports(*request.dependency_set),
                .fsm_factory = {},
                .default_runtime_budget = request.policy_snapshot->runtime_budget(),
            });
            orchestrator->seed_for_test(request.dependency_set->seeded_waiting_session,
                                        request.dependency_set->seeded_checkpoints);

            RuntimeSinkAttachment sink_attachment;
            if (!install_runtime_sink_subscribers(request, result.resolved_profile_id,
                                                  runtime_local_stub_path, result,
                                                  sink_attachment)) {
                emit_facade_init_log(init_logger, request, result);
                return result;
            }

            root_ = RuntimeCompositionRoot{
                .runtime_instance_id = request.runtime_instance_id,
                .profile_id = result.resolved_profile_id,
                .policy_snapshot = request.policy_snapshot,
                .dependency_set = request.dependency_set,
                .orchestrator = std::move(orchestrator),
                .waiting_session = request.dependency_set->seeded_waiting_session,
                .readiness = readiness,
                .degraded = readiness.degraded,
            };
            sink_attachment_ = std::move(sink_attachment);
            initialized_ = true;

            result.accepted = true;
            result.degraded = readiness.degraded;
            result.projected_readiness =
                runtime_local_stub_path ? AgentInitReadinessLevel::StubReady
                : readiness.degraded    ? AgentInitReadinessLevel::DegradedReady
                                        : AgentInitReadinessLevel::DefaultReady;
            append_diagnostic_fragment(result.diagnostics,
                                       std::string{"entrypoint_ready="} +
                                           (runtime_local_stub_path ? "stub-ready"
                                            : readiness.degraded    ? "degraded-ready"
                                                                    : "default-ready"));
            result.health_summary =
                readiness.degraded
                    ? result.degraded_reasons.empty()
                          ? "runtime facade initialized in degraded mode"
                          : std::string{"runtime facade initialized in degraded mode: "} +
                                join_values(result.degraded_reasons)
                : runtime_local_stub_path ? "runtime facade skeleton initialized"
                : result.diagnostics.empty()
                    ? "runtime facade skeleton initialized"
                    : "runtime facade initialized with policy-projected cognition ports";
            emit_facade_init_log(init_logger, request, result);
            return result;
        }

        contracts::AgentResult handle(const contracts::AgentRequest& request) {
            if (!initialized_) {
                auto result = make_failed_result(request.request_id, request.trace_id,
                                                 "runtime facade is not initialized");
                emit_facade_result_log(logger_from_root(root_), runtime_instance_id_attr(root_),
                                       "runtime.facade.handle", request.request_id,
                                       request.session_id, request.trace_id, std::nullopt, result,
                                       "not_initialized", false);
                return result;
            }

            if (!root_.orchestrator) {
                auto result =
                    make_failed_result(request.request_id, request.trace_id,
                                       "runtime facade is missing orchestrator composition");
                emit_facade_result_log(logger_from_root(root_), runtime_instance_id_attr(root_),
                                       "runtime.facade.handle", request.request_id,
                                       request.session_id, request.trace_id, std::nullopt, result,
                                       "missing_orchestrator",
                                       root_.waiting_session.has_value() &&
                                           is_active_waiting_session(*root_.waiting_session));
                return result;
            }

            auto run_result = root_.orchestrator->run_once(request);
            apply_runtime_path_tag(root_, run_result);
            apply_runtime_readiness_tags(root_.readiness, run_result.agent_result);
            update_waiting_session(run_result);
            emit_facade_result_log(logger_from_root(root_), runtime_instance_id_attr(root_),
                                   "runtime.facade.handle", request.request_id, request.session_id,
                                   request.trace_id, run_result.agent_result.checkpoint_ref,
                                   run_result.agent_result, "completed",
                                   root_.waiting_session.has_value() &&
                                       is_active_waiting_session(*root_.waiting_session));
            return run_result.agent_result;
        }

        contracts::AgentResult resume(const ResumeHandleRequest& request) {
            if (!initialized_) {
                auto result = make_failed_result(optional_string(request.request_id),
                                                 optional_string(request.trace_context),
                                                 "runtime facade is not initialized");
                emit_facade_result_log(
                    logger_from_root(root_), runtime_instance_id_attr(root_),
                    "runtime.facade.resume", optional_string(request.request_id),
                    optional_string(request.session_id), optional_string(request.trace_context),
                    optional_string(request.checkpoint_ref), result, "not_initialized", false);
                return result;
            }

            if (!request.has_minimum_requirements()) {
                auto result = make_failed_result(
                    optional_string(request.request_id), optional_string(request.trace_context),
                    "resume request is missing required checkpoint anchors");
                emit_facade_result_log(
                    logger_from_root(root_), runtime_instance_id_attr(root_),
                    "runtime.facade.resume", optional_string(request.request_id),
                    optional_string(request.session_id), optional_string(request.trace_context),
                    optional_string(request.checkpoint_ref), result, "missing_required_anchors",
                    root_.waiting_session.has_value() &&
                        is_active_waiting_session(*root_.waiting_session));
                return result;
            }

            if (!root_.orchestrator) {
                auto result = make_failed_result(
                    optional_string(request.request_id), optional_string(request.trace_context),
                    "runtime facade is missing orchestrator composition");
                emit_facade_result_log(
                    logger_from_root(root_), runtime_instance_id_attr(root_),
                    "runtime.facade.resume", optional_string(request.request_id),
                    optional_string(request.session_id), optional_string(request.trace_context),
                    optional_string(request.checkpoint_ref), result, "missing_orchestrator",
                    root_.waiting_session.has_value() &&
                        is_active_waiting_session(*root_.waiting_session));
                return result;
            }

            SessionSnapshot resume_session;
            if (root_.waiting_session.has_value() &&
                is_active_waiting_session(*root_.waiting_session)) {
                if (root_.waiting_session->session_id != request.session_id) {
                    auto result = make_failed_result(
                        optional_string(request.request_id), optional_string(request.trace_context),
                        "runtime resume request session does not match active waiting session");
                    emit_facade_result_log(
                        logger_from_root(root_), runtime_instance_id_attr(root_),
                        "runtime.facade.resume", optional_string(request.request_id),
                        optional_string(request.session_id), optional_string(request.trace_context),
                        optional_string(request.checkpoint_ref), result, "session_mismatch", true);
                    return result;
                }

                if (root_.waiting_session->active_checkpoint_ref != request.checkpoint_ref) {
                    auto result = make_failed_result(
                        optional_string(request.request_id), optional_string(request.trace_context),
                        "runtime resume request checkpoint does not match active waiting anchor");
                    emit_facade_result_log(
                        logger_from_root(root_), runtime_instance_id_attr(root_),
                        "runtime.facade.resume", optional_string(request.request_id),
                        optional_string(request.session_id), optional_string(request.trace_context),
                        optional_string(request.checkpoint_ref), result, "checkpoint_mismatch",
                        true);
                    return result;
                }

                resume_session = *root_.waiting_session;
            } else {
                resume_session = SessionSnapshot{
                    .session_id = request.session_id,
                    .request_id = request.request_id,
                    .turn_index = 0,
                    .active_checkpoint_ref = request.checkpoint_ref,
                    .fsm_state = RuntimeState::WaitingClarify,
                    .budget_snapshot_ref = std::nullopt,
                    .pending_interaction = std::nullopt,
                    .last_result_summary = std::nullopt,
                };
            }

            if (request.resume_token !=
                make_resume_binding_token(request.session_id, request.checkpoint_ref)) {
                auto result = make_failed_result(
                    optional_string(request.request_id), optional_string(request.trace_context),
                    "runtime resume request token does not match waiting checkpoint binding");
                emit_facade_result_log(
                    logger_from_root(root_), runtime_instance_id_attr(root_),
                    "runtime.facade.resume", optional_string(request.request_id),
                    optional_string(request.session_id), optional_string(request.trace_context),
                    optional_string(request.checkpoint_ref), result, "token_mismatch",
                    root_.waiting_session.has_value() &&
                        is_active_waiting_session(*root_.waiting_session));
                return result;
            }

            auto run_result = root_.orchestrator->handle_waiting_state(resume_session, request);
            apply_runtime_path_tag(root_, run_result);
            apply_runtime_readiness_tags(root_.readiness, run_result.agent_result);
            update_waiting_session(run_result);
            emit_facade_result_log(
                logger_from_root(root_), runtime_instance_id_attr(root_), "runtime.facade.resume",
                optional_string(request.request_id), optional_string(request.session_id),
                optional_string(request.trace_context), optional_string(request.checkpoint_ref),
                run_result.agent_result, "completed",
                root_.waiting_session.has_value() &&
                    is_active_waiting_session(*root_.waiting_session));
            return run_result.agent_result;
        }

        bool stop(std::uint32_t timeout_ms) {
            emit_facade_stop_log(logger_from_root(root_), runtime_instance_id_attr(root_),
                                 initialized_, timeout_ms);
            unsubscribe_runtime_sink_subscribers(root_, sink_attachment_);
            initialized_ = false;
            root_ = RuntimeCompositionRoot{};
            return true;
        }

      private:
        void update_waiting_session(const OrchestratorRunResult& run_result) {
            if (!run_result.effective_session.has_value()) {
                return;
            }

            if (is_active_waiting_session(*run_result.effective_session)) {
                root_.waiting_session = run_result.effective_session;
                return;
            }

            root_.waiting_session = std::nullopt;
        }

        bool initialized_ = false;
        RuntimeCompositionRoot root_;
        RuntimeSinkAttachment sink_attachment_;
    };

    AgentFacade::AgentFacade() : state_(std::make_unique<State>()) {}

    AgentFacade::~AgentFacade() = default;

    AgentInitResult AgentFacade::init(const AgentInitRequest& request) {
        return state_->init(request);
    }

    contracts::AgentResult AgentFacade::handle(const contracts::AgentRequest& request) {
        return state_->handle(request);
    }

    contracts::AgentResult AgentFacade::resume(const ResumeHandleRequest& request) {
        return state_->resume(request);
    }

    bool AgentFacade::stop(std::uint32_t timeout_ms) {
        return state_->stop(timeout_ms);
    }

} // namespace dasall::runtime