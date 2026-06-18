#include "ICognitionEngine.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "RuntimePolicySnapshot.h"
#include "StagePolicyResolver.h"
#include "belief/BeliefUpdateSynthesizer.h"
#include "config/CognitionConfigProjector.h"
#include "llm/CognitionLlmBridge.h"
#include "observability/CognitionTelemetry.h"
#include "perception/PerceptionEngine.h"
#include "planning/Planner.h"
#include "projection/ActionDecisionStructuredProjector.h"
#include "projection/PlanGraphStructuredProjector.h"
#include "reasoning/Reasoner.h"
#include "reflection/ReflectionEngine.h"
#include "validation/InputBoundaryValidator.h"
#include "validation/StageSchemaRegistry.h"
#include "validation/StageOutputValidator.h"
#include "validation/StructuredPayloadView.h"

namespace dasall::cognition {
    namespace {

        using decision::ActionDecision;
        using decision::ActionDecisionKind;
        using llm_bridge::CognitionLlmBridge;
        using llm_bridge::StageLlmCallResult;
        using observability::DecisionTelemetryRecord;
        using observability::StageTelemetryContext;
        using observability::StructuredProjectionTelemetry;
        using observability::TelemetryEmitResult;
        using observability::TelemetryField;
        using policy::StageExecutionPlan;
        using validation::InputBoundaryValidationResult;

        /// @brief Represents the result of a stage execution, including timing information and the
        /// stage's output.
        /// @tparam T The type of the stage's output.
        template <typename T>

        /// @brief Represents the result of a stage execution, including timing information and the
        /// stage's output.
        struct TimedStageResult {
            bool timed_out = false;
            std::uint32_t elapsed_ms = 0;
            std::optional<T> value;
        };

        /**
         * @brief Runs a given stage function with a specified deadline. If the function does not
         * complete within the deadline, it will be terminated and an optional timeout callback will
         * be invoked. The result of the function execution, along with timing information, is
         * returned in a TimedStageResult structure. This utility is useful for enforcing time
         * constraints on different stages of the cognition pipeline, ensuring that the system
         * remains responsive and can handle cases where certain stages may take longer than
         * expected to complete.
         *
         * @tparam Fn The type of the function to be executed.
         * @param deadline_ms The deadline in milliseconds for the function to complete.
         * @param fn The function to be executed.
         * @param on_timeout An optional callback to be invoked if the function times out.
         * @return TimedStageResult<std::invoke_result_t<Fn>> The result of the function execution
         * along with timing information.
         */
        template <typename Fn>
        [[nodiscard]] auto run_stage_with_deadline(std::uint32_t deadline_ms, Fn&& fn,
                                                   std::function<void()> on_timeout = {})
            -> TimedStageResult<std::invoke_result_t<Fn>> {
            using ReturnType = std::invoke_result_t<Fn>;
            const auto started_at = std::chrono::steady_clock::now();

            // If no deadline is set, run the function synchronously and return the result
            // immediately.
            if (deadline_ms == 0U) {
                return TimedStageResult<ReturnType>{
                    .timed_out = false,
                    .elapsed_ms = static_cast<std::uint32_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started_at)
                            .count()),
                    .value = std::invoke(std::forward<Fn>(fn)),
                };
            }

            // Run the function in a separate thread and wait for it to complete or for the deadline
            // to expire.
            std::promise<ReturnType> promise;
            auto future = promise.get_future();
            std::thread worker([promise = std::move(promise), fn = std::forward<Fn>(fn)]() mutable {
                try {
                    promise.set_value(std::invoke(std::move(fn)));
                } catch (...) {
                    try {
                        promise.set_exception(std::current_exception());
                    } catch (...) {
                    }
                }
            });

            // Wait for the function to complete or for the deadline to expire.
            // If the function completes in time, join the worker thread and return the result.
            // If the deadline expires first, detach the worker thread and invoke the timeout
            // callback if provided.
            if (future.wait_for(std::chrono::milliseconds(deadline_ms)) ==
                std::future_status::ready) {
                try {
                    auto value = future.get();
                    if (worker.joinable()) {
                        worker.join();
                    }
                    return TimedStageResult<ReturnType>{
                        .timed_out = false,
                        .elapsed_ms = static_cast<std::uint32_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started_at)
                                .count()),
                        .value = std::move(value),
                    };
                } catch (...) {
                    if (worker.joinable()) {
                        worker.join();
                    }
                    throw;
                }
            }

            if (worker.joinable()) {
                worker.detach();
            }

            // Invoke the timeout callback if provided.
            if (on_timeout) {
                std::thread timeout_notifier([on_timeout = std::move(on_timeout)]() mutable {
                    try {
                        on_timeout();
                    } catch (...) {
                    }
                });
                timeout_notifier.detach();
            }

            return TimedStageResult<ReturnType>{
                .timed_out = true,
                .elapsed_ms = static_cast<std::uint32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started_at)
                        .count()),
                .value = std::nullopt,
            };
        }

        /**
         * @brief Calculate the elapsed time in milliseconds since the given start time.
         *
         * @param started_at The start time point.
         * @return std::uint32_t The elapsed time in milliseconds.
         */
        [[nodiscard]] std::uint32_t
        elapsed_ms_since(const std::chrono::steady_clock::time_point& started_at) {
            return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                  std::chrono::steady_clock::now() - started_at)
                                                  .count());
        }

        /**
         * @brief Get the default response summary for a given cognition step request.
         *
         * @param request The cognition step request.
         * @return std::string The default response summary.
         */
        [[nodiscard]] std::string default_response_summary(const CognitionStepRequest& request) {
            if (request.context_packet.current_goal_summary.has_value() &&
                !request.context_packet.current_goal_summary->empty()) {
                return *request.context_packet.current_goal_summary;
            }

            return *request.goal_contract.goal_description;
        }

        /**
         * @brief Get the goal ID from the given goal contract.
         * This function retrieves the goal ID from the provided goal contract. If the goal ID is not
         * present, it returns an empty string. This utility function is used to ensure that the goal ID is
         * consistently extracted from the goal contract across different parts of the cognition pipeline, 
         * and it helps to avoid code duplication when accessing the goal ID. By centralizing this logic 
         * in a single function, we can also easily modify the behavior of how goal IDs are retrieved in 
         * the future if needed, without having to change code in multiple places.
         * 
         * @param goal_contract 
         * @return std::string 
         */
        [[nodiscard]] std::string require_goal_id(const contracts::GoalContract& goal_contract) {
            return goal_contract.goal_id.value_or(std::string{});
        }

        /**
         * @brief Derive the model hint tier based on the given cognition step request.
         * This function determines the appropriate model hint tier for a cognition step request
         * based on the execution hints and budget context. It helps in selecting the right
         * model configuration for the request.
         *
         * @param request The cognition step request.
         * @return std::string The derived model hint tier.
         */
        [[nodiscard]] std::string derive_model_hint_tier(const CognitionStepRequest& request) {
            if (request.execution_hints.low_latency_preferred) {
                return "economy";
            }

            if (request.budget_context.has_value() && request.budget_context->near_budget_limit) {
                return "balanced";
            }

            return "standard";
        }

        /**
         * @brief Derive the model hint tier based on the given reflection request.
         * This function determines the appropriate model hint tier for a reflection request
         * based on the policy digest. It helps in selecting the right model configuration for the request.
         *
         * @param request The reflection request.
         * @return std::string The derived model hint tier.
         */
        [[nodiscard]] std::string derive_model_hint_tier(const ReflectionRequest& request) {
            if (request.context_packet.policy_digest.has_value() &&
                request.context_packet.policy_digest->find("economy") != std::string::npos) {
                return "economy";
            }

            return "standard";
        }

        /**
         * @brief Create a stage telemetry context for a cognition step request.
         * This function constructs a StageTelemetryContext object for a given cognition step request,
         * including information about the request, stage, fallback usage, result code, and structured projection.
         *
         * @param request The cognition step request.
         * @param stage The current stage of the request.
         * @param fallback_used Indicates if a fallback was used.
         * @param result_code Optional result code.
         * @param structured_projection Optional structured projection telemetry.
         * @return StageTelemetryContext The constructed stage telemetry context.
         */
        [[nodiscard]] StageTelemetryContext
        make_stage_context(const CognitionStepRequest& request, std::string stage,
                           bool fallback_used,
                           std::optional<contracts::ResultCode> result_code = std::nullopt,
                           StructuredProjectionTelemetry structured_projection = {}) {
            return StageTelemetryContext{
                .request_id = request.request_id,
                .goal_id = require_goal_id(request.goal_contract),
                .profile_id = request.profile_id,
                .stage = std::move(stage),
                .trace_id = request.trace_id,
                .model_hint_tier = derive_model_hint_tier(request),
                .fallback_used = fallback_used,
                .result_code = result_code.has_value()
                                   ? std::optional<int>(static_cast<int>(*result_code))
                                   : std::nullopt,
                .structured_projection = std::move(structured_projection),
            };
        }

        /**
         * @brief Create a stage telemetry context for a reflection request.
         * This function constructs a StageTelemetryContext object for a given reflection request,
         * including information about the request, stage, fallback usage, result code, and structured projection.
         *
         * @param request The reflection request.
         * @param stage The current stage of the request.
         * @param fallback_used Indicates if a fallback was used.
         * @param result_code Optional result code.
         * @param structured_projection Optional structured projection telemetry.
         * @return StageTelemetryContext The constructed stage telemetry context.
         */
        [[nodiscard]] StageTelemetryContext
        make_stage_context(const ReflectionRequest& request, std::string stage, bool fallback_used,
                           std::optional<contracts::ResultCode> result_code = std::nullopt,
                           StructuredProjectionTelemetry structured_projection = {}) {
            return StageTelemetryContext{
                .request_id = request.request_id,
                .goal_id = require_goal_id(request.goal_contract),
                .profile_id = request.profile_id,
                .stage = std::move(stage),
                .trace_id = request.trace_id,
                .model_hint_tier = derive_model_hint_tier(request),
                .fallback_used = fallback_used,
                .result_code = result_code.has_value()
                                   ? std::optional<int>(static_cast<int>(*result_code))
                                   : std::nullopt,
                .structured_projection = std::move(structured_projection),
            };
        }

        /**
         * @brief Append a unique value to a vector of strings.
         * This function adds the given value to the target vector only if it is not already present.
         *
         * @param target The vector of strings to which the value will be appended.
         * @param value The value to append.
         */
        void append_unique(std::vector<std::string>& target, std::string value) {
            if (value.empty()) {
                return;
            }

            if (std::find(target.begin(), target.end(), value) == target.end()) {
                target.push_back(std::move(value));
            }
        }

        /**
         * @brief Append unique values from a vector to another vector of strings.
         * This function adds each value from the source vector to the target vector only if it is not already present.
         *
         * @param target The vector of strings to which the values will be appended.
         * @param values The vector of values to append.
         */
        void append_unique(std::vector<std::string>& target,
                           const std::vector<std::string>& values) {
            for (const auto& value : values) {
                append_unique(target, value);
            }
        }

        [[nodiscard]] std::optional<std::string>
        synthesize_reflection_trigger_condition(const ReflectionRequest& request) {
            if (request.latest_observation.error.has_value() &&
                !request.latest_observation.error->details.message.empty()) {
                return request.latest_observation.error->details.message;
            }

            if (request.latest_observation.payload.has_value() &&
                !request.latest_observation.payload->empty()) {
                return request.latest_observation.payload;
            }

            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string>
        synthesize_reflection_recommended_action(const contracts::ReflectionDecision& decision) {
            if (!decision.decision_kind.has_value()) {
                return std::nullopt;
            }

            switch (*decision.decision_kind) {
                case contracts::ReflectionDecisionKind::RetryStep:
                    return std::string{"retry the failed step after rechecking the latest evidence"};
                case contracts::ReflectionDecisionKind::Replan:
                    return std::string{"replan before executing the next action"};
                case contracts::ReflectionDecisionKind::AbortSafe:
                    return std::string{"stop the unsafe path and switch to a safe fallback"};
                case contracts::ReflectionDecisionKind::Continue:
                case contracts::ReflectionDecisionKind::Unspecified:
                default:
                    return std::nullopt;
            }
        }

        [[nodiscard]] std::optional<contracts::ReflectionLessonProjection>
        synthesize_reflection_lesson_projection(const ReflectionRequest& request,
                                                const contracts::ReflectionDecision& decision) {
            if (!decision.decision_kind.has_value() ||
                *decision.decision_kind == contracts::ReflectionDecisionKind::Continue ||
                !decision.rationale.has_value() || decision.rationale->empty()) {
                return std::nullopt;
            }

            contracts::ReflectionLessonProjection lesson;
            lesson.lesson_summary = decision.rationale;
            lesson.trigger_condition = synthesize_reflection_trigger_condition(request);
            lesson.recommended_action = synthesize_reflection_recommended_action(decision);
            lesson.effectiveness_score = static_cast<std::uint32_t>(
                std::clamp(decision.confidence.value_or(0.0F), 0.0F, 1.0F) * 100.0F);
            lesson.applicable_domains = std::vector<std::string>{"reflection"};
            lesson.tags = std::vector<std::string>{
                "reflection",
                "stage:reflection",
                "experience_kind:self_reflection",
                "cognition",
            };
            return lesson;
        }

        /**
         * @brief Create a diagnostic string for a structured projection flag.
         * This function constructs a diagnostic string for a structured projection flag
         * based on the given field and stage.
         *
         * @param field The field name.
         * @param stage The stage name.
         * @return std::string The constructed diagnostic string.
         */
        [[nodiscard]] std::string
        structured_projection_flag_diagnostic(const std::string_view field,
                                              const std::string_view stage) {
            return std::string{"structured_projection."} + std::string(field) + ":" +
                   std::string(stage);
        }

        /**
         * @brief Create a diagnostic string for a structured projection value.
         * This function constructs a diagnostic string for a structured projection value
         * based on the given field, stage, and value.
         *
         * @param field The field name.
         * @param stage The stage name.
         * @param value The value.
         * @return std::string The constructed diagnostic string.
         */
        [[nodiscard]] std::string
        structured_projection_value_diagnostic(const std::string_view field,
                                               const std::string_view stage,
                                               const std::string_view value) {
            return structured_projection_flag_diagnostic(field, stage) + ":" + std::string(value);
        }

        /**
         * @brief Append a structured projection flag to the diagnostics vector.
         * This function appends a structured projection flag to the diagnostics vector
         * based on the given field and stage.
         *
         * @param diagnostics The vector of diagnostics to which the flag will be appended.
         * @param field The field name.
         * @param stage The stage name.
         */
        void append_structured_projection_flag(std::vector<std::string>& diagnostics,
                                               const std::string_view field,
                                               const std::string_view stage) {
            append_unique(diagnostics, structured_projection_flag_diagnostic(field, stage));
        }

        /**
         * @brief Append a structured projection value to the diagnostics vector.
         * This function appends a structured projection value to the diagnostics vector
         * based on the given field, stage, and value.
         *
         * @param diagnostics The vector of diagnostics to which the value will be appended.
         * @param field The field name.
         * @param stage The stage name.
         * @param value The value to append.
         */
        void append_structured_projection_value(std::vector<std::string>& diagnostics,
                                                const std::string_view field,
                                                const std::string_view stage,
                                                const std::string& value) {
            if (value.empty()) {
                return;
            }

            append_unique(diagnostics, structured_projection_value_diagnostic(field, stage, value));
        }

        /**
         * @brief Check if a structured projection flag exists in the diagnostics vector.
         * This function checks if a structured projection flag exists in the diagnostics vector
         * based on the given field and stage.
         *
         * @param diagnostics The vector of diagnostics to check.
         * @param field The field name.
         * @param stage The stage name.
         * @return true If the flag exists.
         * @return false If the flag does not exist.
         */
        [[nodiscard]] bool
        has_structured_projection_flag(const std::vector<std::string>& diagnostics,
                                       const std::string_view field, const std::string_view stage) {
            const auto needle = structured_projection_flag_diagnostic(field, stage);
            return std::find(diagnostics.begin(), diagnostics.end(), needle) != diagnostics.end();
        }

        /**
         * @brief Find a structured projection value in the diagnostics vector.
         * This function finds a structured projection value in the diagnostics vector
         * based on the given field and stage.
         *
         * @param diagnostics The vector of diagnostics to search.
         * @param field The field name.
         * @param stage The stage name.
         * @return std::optional<std::string> The found value, or std::nullopt if not found.
         */
        [[nodiscard]] std::optional<std::string>
        find_structured_projection_value(const std::vector<std::string>& diagnostics,
                                         const std::string_view field,
                                         const std::string_view stage) {
            const auto prefix = structured_projection_flag_diagnostic(field, stage) + ":";
            for (auto it = diagnostics.rbegin(); it != diagnostics.rend(); ++it) {
                if (it->rfind(prefix, 0) == 0) {
                    return it->substr(prefix.size());
                }
            }

            return std::nullopt;
        }

        /**
         * @brief Find a structured projection count in the diagnostics vector.
         * This function finds a structured projection count in the diagnostics vector
         * based on the given field and stage.
         *
         * @param diagnostics The vector of diagnostics to search.
         * @param field The field name.
         * @param stage The stage name.
         * @return std::optional<std::uint32_t> The found count, or std::nullopt if not found.
         */
        [[nodiscard]] std::optional<std::uint32_t>
        find_structured_projection_count(const std::vector<std::string>& diagnostics,
                                         const std::string_view field,
                                         const std::string_view stage) {
            const auto value = find_structured_projection_value(diagnostics, field, stage);
            if (!value.has_value()) {
                return std::nullopt;
            }

            try {
                return static_cast<std::uint32_t>(std::stoul(*value));
            } catch (...) {
                return std::nullopt;
            }
        }

        /**
         * @brief Summarize structured projection telemetry from the diagnostics vector.
         * This function summarizes structured projection telemetry from the diagnostics vector.
         *
         * @param diagnostics The vector of diagnostics to summarize.
         * @return StructuredProjectionTelemetry The summarized structured projection telemetry.
         */
        [[nodiscard]] StructuredProjectionTelemetry
        summarize_structured_projection_telemetry(const std::vector<std::string>& diagnostics) {
            StructuredProjectionTelemetry structured_projection;
            structured_projection.enabled =
                has_structured_projection_flag(diagnostics, "enabled", "perception") ||
                has_structured_projection_flag(diagnostics, "enabled", "planning") ||
                has_structured_projection_flag(diagnostics, "enabled", "execution");
            structured_projection.required =
                has_structured_projection_flag(diagnostics, "required", "perception") ||
                has_structured_projection_flag(diagnostics, "required", "planning") ||
                has_structured_projection_flag(diagnostics, "required", "execution");
            structured_projection.schema_version =
                find_structured_projection_value(diagnostics, "schema_version", "execution");
            if (!structured_projection.schema_version.has_value()) {
                structured_projection.schema_version =
                    find_structured_projection_value(diagnostics, "schema_version", "planning");
            }
            if (!structured_projection.schema_version.has_value()) {
                structured_projection.schema_version =
                    find_structured_projection_value(diagnostics, "schema_version", "perception");
            }
            structured_projection.source =
                find_structured_projection_value(diagnostics, "source", "execution");
            if (!structured_projection.source.has_value()) {
                structured_projection.source =
                    find_structured_projection_value(diagnostics, "source", "planning");
            }
            if (!structured_projection.source.has_value()) {
                structured_projection.source =
                    find_structured_projection_value(diagnostics, "source", "perception");
            }
            structured_projection.failure_code =
                find_structured_projection_value(diagnostics, "failure_code", "execution");
            if (!structured_projection.failure_code.has_value()) {
                structured_projection.failure_code =
                    find_structured_projection_value(diagnostics, "failure_code", "planning");
            }
            if (!structured_projection.failure_code.has_value()) {
                structured_projection.failure_code =
                    find_structured_projection_value(diagnostics, "failure_code", "perception");
            }
            structured_projection.projected_node_count =
                find_structured_projection_count(diagnostics, "projected_node_count", "planning");
            structured_projection.projected_candidate_count = find_structured_projection_count(
                diagnostics, "projected_candidate_count", "execution");
            return structured_projection;
        }

        /**
         * @brief Ignore the result of a telemetry emit operation.
         *
         * @param result The telemetry emit result to ignore.
         */
        void ignore_emit_result(TelemetryEmitResult) {}

        /**
         * @brief Convert a boolean value to its string representation for replay.
         *
         * @param value The boolean value to convert.
         * @return std::string The string representation of the boolean value.
         */
        [[nodiscard]] std::string replay_bool_value(const bool value) {
            return value ? "true" : "false";
        }

        /**
         * @brief Escape special characters in a string for replay.
         *
         * @param value The string value to escape.
         * @return std::string The escaped string value.
         */
        [[nodiscard]] std::string escape_replay_value(std::string_view value) {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char ch : value) {
                switch (ch) {
                case '\\':
                    escaped += "\\\\";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                default:
                    escaped.push_back(ch);
                    break;
                }
            }
            return escaped;
        }

        /**
         * @brief Append a replay line to the serialized string.
         *
         * @param serialized The string to append the replay line to.
         * @param key The key of the replay line.
         * @param value The value of the replay line.
         */
        void append_replay_line(std::string& serialized, std::string_view key,
                                std::string_view value) {
            serialized += key;
            serialized += '=';
            serialized += escape_replay_value(value);
            serialized += '\n';
        }

        /**
         * @brief Append a replay line for a numeric value to the serialized string.
         *
         * @tparam Number The type of the numeric value.
         * @param serialized The string to append the replay line to.
         * @param key The key of the replay line.
         * @param value The numeric value to append.
         */
        template <typename Number>
        void append_replay_number_line(std::string& serialized, std::string_view key,
                                       Number value) {
            append_replay_line(serialized, key, std::to_string(value));
        }

        /**
         * @brief Append a replay line for an optional value to the serialized string.
         *
         * @param serialized The string to append the replay line to.
         * @param key The key of the replay line.
         * @param value The optional value to append.
         */
        void append_replay_optional_line(std::string& serialized, std::string_view key,
                                         const std::optional<std::string>& value) {
            if (value.has_value()) {
                append_replay_line(serialized, key, *value);
            }
        }

        /**
         * @brief Append sorted values to the serialized string.
         *
         * @param serialized The string to append the sorted values to.
         * @param prefix The prefix for the keys of the sorted values.
         * @param values The vector of values to sort and append.
         */
        void append_replay_sorted_values(std::string& serialized, std::string_view prefix,
                                         const std::vector<std::string>& values) {
            auto sorted_values = values;
            std::sort(sorted_values.begin(), sorted_values.end());
            append_replay_number_line(serialized, std::string(prefix) + "_count",
                                      sorted_values.size());
            for (std::size_t index = 0; index < sorted_values.size(); ++index) {
                append_replay_line(serialized, std::string(prefix) + "." + std::to_string(index),
                                   sorted_values[index]);
            }
        }

        /**
         * @brief Get the name of an action decision kind.
         *
         * @param kind The action decision kind.
         * @return std::string The name of the action decision kind.
         */
        [[nodiscard]] std::string replay_action_decision_kind_name(const ActionDecisionKind kind) {
            switch (kind) {
            case ActionDecisionKind::NoDecision:
                return "NoDecision";
            case ActionDecisionKind::AskClarification:
                return "AskClarification";
            case ActionDecisionKind::ExecuteAction:
                return "ExecuteAction";
            case ActionDecisionKind::DirectResponse:
                return "DirectResponse";
            case ActionDecisionKind::ConvergeSafe:
                return "ConvergeSafe";
            }

            return "Unknown";
        }

        /**
         * @brief Get the name of a reflection decision kind.
         *
         * @param kind The reflection decision kind.
         * @return std::string The name of the reflection decision kind.
         */
        [[nodiscard]] std::string
        replay_reflection_decision_kind_name(const contracts::ReflectionDecisionKind kind) {
            switch (kind) {
            case contracts::ReflectionDecisionKind::Unspecified:
                return "Unspecified";
            case contracts::ReflectionDecisionKind::Continue:
                return "Continue";
            case contracts::ReflectionDecisionKind::RetryStep:
                return "RetryStep";
            case contracts::ReflectionDecisionKind::Replan:
                return "Replan";
            case contracts::ReflectionDecisionKind::AbortSafe:
                return "AbortSafe";
            }

            return "Unknown";
        }

        /**
         * @brief Serialize a decide request into a string.
         *
         * @param request The decide request to serialize.
         * @return std::string The serialized decide request.
         */
        [[nodiscard]] std::string serialize_decide_request(const CognitionStepRequest& request) {
            std::string serialized;
            append_replay_line(serialized, "surface", "decide.request");
            append_replay_line(serialized, "caller_domain", request.caller_domain);
            append_replay_line(serialized, "request_id", request.request_id);
            append_replay_line(serialized, "trace_id", request.trace_id);
            append_replay_line(serialized, "profile_id", request.profile_id);
            append_replay_optional_line(serialized, "goal_id", request.goal_contract.goal_id);
            append_replay_optional_line(serialized, "goal_description",
                                        request.goal_contract.goal_description);
            append_replay_optional_line(serialized, "user_turn", request.context_packet.user_turn);
            append_replay_optional_line(serialized, "current_goal_summary",
                                        request.context_packet.current_goal_summary);
            append_replay_number_line(serialized, "belief_confidence",
                                      request.belief_state.confidence.value_or(0.0F));
            append_replay_line(serialized, "latest_observation_present",
                               replay_bool_value(request.latest_observation.has_value()));
            if (request.latest_observation.has_value()) {
                append_replay_line(
                    serialized, "latest_observation_success",
                    replay_bool_value(request.latest_observation->success.value_or(false)));
                append_replay_optional_line(serialized, "latest_observation_payload",
                                            request.latest_observation->payload);
            }
            if (request.budget_context.has_value()) {
                append_replay_number_line(serialized, "remaining_tokens",
                                          request.budget_context->remaining_tokens);
            }
            append_replay_line(serialized, "degraded_path_allowed",
                               replay_bool_value(request.execution_hints.degraded_path_allowed));
            append_replay_line(serialized, "low_latency_preferred",
                               replay_bool_value(request.execution_hints.low_latency_preferred));
            append_replay_number_line(serialized, "risk_tolerance",
                                      request.execution_hints.risk_tolerance);
            return serialized;
        }

        /**
         * @brief Serialize a decision result into a string.
         *
         * @param result The decision result to serialize.
         * @return std::string The serialized decision result.
         */
        [[nodiscard]] std::string serialize_decision_result(const CognitionDecisionResult& result) {
            std::string serialized;
            append_replay_line(serialized, "surface", "decide.result");
            if (result.result_code.has_value()) {
                append_replay_number_line(serialized, "result_code",
                                          static_cast<int>(*result.result_code));
            }
            if (result.action_decision.has_value()) {
                append_replay_line(
                    serialized, "decision_kind",
                    replay_action_decision_kind_name(result.action_decision->decision_kind));
                append_replay_optional_line(serialized, "selected_node_id",
                                            result.action_decision->selected_node_id);
                append_replay_line(serialized, "clarification_needed",
                                   replay_bool_value(result.action_decision->clarification_needed));
                append_replay_number_line(serialized, "candidate_score_count",
                                          result.action_decision->candidate_scores.size());
                append_replay_line(
                    serialized, "has_tool_intent_hint",
                    replay_bool_value(result.action_decision->tool_intent_hint.has_value()));
                append_replay_line(
                    serialized, "has_response_outline",
                    replay_bool_value(result.action_decision->response_outline.has_value()));
            }
            append_replay_line(serialized, "context_sufficient",
                               replay_bool_value(result.context_sufficiency.context_sufficient));
            append_replay_number_line(serialized, "context_confidence",
                                      result.context_sufficiency.context_confidence);
            append_replay_line(
                serialized, "recommend_context_reload",
                replay_bool_value(result.context_sufficiency.recommend_context_reload));
            append_replay_sorted_values(serialized, "diagnostic", result.diagnostics);
            return serialized;
        }

        /**
         * @brief Serialize a reflection request into a string.
         *
         * @param request The reflection request to serialize.
         * @return std::string The serialized reflection request.
         */
        [[nodiscard]] std::string serialize_reflection_request(const ReflectionRequest& request) {
            std::string serialized;
            append_replay_line(serialized, "surface", "reflect.request");
            append_replay_line(serialized, "caller_domain", request.caller_domain);
            append_replay_line(serialized, "request_id", request.request_id);
            append_replay_line(serialized, "trace_id", request.trace_id);
            append_replay_line(serialized, "profile_id", request.profile_id);
            append_replay_optional_line(serialized, "goal_id", request.goal_contract.goal_id);
            append_replay_optional_line(serialized, "goal_description",
                                        request.goal_contract.goal_description);
            append_replay_optional_line(serialized, "user_turn", request.context_packet.user_turn);
            append_replay_line(
                serialized, "latest_observation_success",
                replay_bool_value(request.latest_observation.success.value_or(false)));
            append_replay_optional_line(serialized, "latest_observation_payload",
                                        request.latest_observation.payload);
            append_replay_optional_line(serialized, "active_plan_ref", request.active_plan_ref);
            append_replay_line(serialized, "degraded_path_allowed",
                               replay_bool_value(request.execution_hints.degraded_path_allowed));
            append_replay_line(serialized, "low_latency_preferred",
                               replay_bool_value(request.execution_hints.low_latency_preferred));
            return serialized;
        }

        /**
         * @brief Serialize a reflection result into a string.
         *
         * @param result The reflection result to serialize.
         * @return std::string The serialized reflection result.
         */
        [[nodiscard]] std::string
        serialize_reflection_result(const CognitionReflectionResult& result) {
            std::string serialized;
            append_replay_line(serialized, "surface", "reflect.result");
            if (result.result_code.has_value()) {
                append_replay_number_line(serialized, "result_code",
                                          static_cast<int>(*result.result_code));
            }
            if (result.reflection_decision.has_value()) {
                append_replay_line(serialized, "decision_kind",
                                   replay_reflection_decision_kind_name(
                                       result.reflection_decision->decision_kind.value_or(
                                           contracts::ReflectionDecisionKind::Unspecified)));
                append_replay_line(
                    serialized, "has_rationale",
                    replay_bool_value(result.reflection_decision->rationale.has_value()));
                append_replay_number_line(
                    serialized, "observation_ref_count",
                    result.reflection_decision->relevant_observation_refs.has_value()
                        ? result.reflection_decision->relevant_observation_refs->size()
                        : 0U);
            }
            append_replay_sorted_values(serialized, "diagnostic", result.diagnostics);
            return serialized;
        }

        /**
         * @brief Serialize a bridge payload into a string.
         *
         * @param surface The surface name.
         * @param stage The stage name.
         * @param bridge_result The bridge result to serialize.
         * @return std::string The serialized bridge payload.
         */
        [[nodiscard]] std::string
        serialize_bridge_payload(std::string_view surface, std::string_view stage,
                                 const StageLlmCallResult& bridge_result) {
            std::string serialized;
            append_replay_line(serialized, "surface", surface);
            append_replay_line(serialized, "stage", stage);
            append_replay_line(serialized, "resolved_route", bridge_result.resolved_route);
            append_replay_sorted_values(serialized, "warning", bridge_result.warnings);
            append_replay_sorted_values(serialized, "diagnostic", bridge_result.diagnostics);
            if (bridge_result.response.has_value() &&
                bridge_result.response->content_payload.has_value()) {
                append_replay_line(serialized, "payload", *bridge_result.response->content_payload);
            }
            return serialized;
        }

        /**
         * @brief Emit a replay trace event.
         *
         * @param telemetry The telemetry instance.
         * @param event_name The name of the event.
         * @param context The stage telemetry context.
         * @param serialized_value The serialized value to include in the event.
         */
        void emit_replay_trace(const observability::CognitionTelemetry& telemetry,
                               std::string event_name, const StageTelemetryContext& context,
                               std::string serialized_value) {
            std::vector<TelemetryField> fields;
            fields.push_back(TelemetryField{
                .key = "serialized_value",
                .value = std::move(serialized_value),
            });
            ignore_emit_result(
                telemetry.emit_detail_event(std::move(event_name), context, std::move(fields)));
        }

        /**
         * @brief Check if a diagnostic is present in the diagnostics list.
         *
         * @param diagnostics The list of diagnostics.
         * @param diagnostic The diagnostic to check for.
         * @return true If the diagnostic is present.
         * @return false If the diagnostic is not present.
         */
        [[nodiscard]] bool has_diagnostic(const std::vector<std::string>& diagnostics,
                                          const std::string_view diagnostic) {
            return std::find(diagnostics.begin(), diagnostics.end(), diagnostic) !=
                   diagnostics.end();
        }

        /**
         * @brief Append a detail field to the list of telemetry fields.
         *
         * @param fields The list of telemetry fields.
         * @param key The key of the detail field.
         * @param value The value of the detail field.
         */
        void append_detail_field(std::vector<TelemetryField>& fields, std::string key,
                                 std::string value) {
            if (value.empty()) {
                return;
            }

            fields.push_back(TelemetryField{
                .key = std::move(key),
                .value = std::move(value),
            });
        }

        /**
         * @brief Emit a pipeline checkpoint event.
         *
         * @param telemetry The telemetry instance.
         * @param context The stage telemetry context.
         * @param pipeline The name of the pipeline.
         * @param step The name of the step.
         * @param outcome The outcome of the checkpoint.
         * @param extra_fields Additional telemetry fields to include.
         */
        void emit_pipeline_checkpoint(const observability::CognitionTelemetry& telemetry,
                                      const StageTelemetryContext& context, std::string pipeline,
                                      std::string step, std::string outcome,
                                      std::vector<TelemetryField> extra_fields = {}) {
            std::vector<TelemetryField> fields;
            fields.reserve(extra_fields.size() + 3U);
            append_detail_field(fields, "pipeline", std::move(pipeline));
            append_detail_field(fields, "step", std::move(step));
            append_detail_field(fields, "outcome", std::move(outcome));
            for (auto& field : extra_fields) {
                append_detail_field(fields, std::move(field.key), std::move(field.value));
            }

            ignore_emit_result(
                telemetry.emit_detail_event("pipeline.checkpoint", context, std::move(fields)));
        }

        /**
         * @brief Append error information fields to the list of telemetry fields.
         *
         * @param fields The list of telemetry fields.
         * @param error_info The error information to append.
         */
        void append_error_info_fields(std::vector<TelemetryField>& fields,
                                      const contracts::ErrorInfo& error_info) {
            if (error_info.failure_type.has_value()) {
                append_detail_field(
                    fields, "error_type",
                    std::string(contracts::result_code_category_name(*error_info.failure_type)));
            }
            if (error_info.details.code.has_value()) {
                append_detail_field(fields, "error_code", std::to_string(*error_info.details.code));
            }
            append_detail_field(fields, "error_stage", error_info.details.stage);
            append_detail_field(fields, "error_message", error_info.details.message);
        }

        /**
         * @brief Find the value of a diagnostic with a specific prefix.
         *
         * @param diagnostics The list of diagnostics.
         * @param prefix The prefix to search for.
         * @return std::optional<std::string> The value of the diagnostic if found.
         */
        [[nodiscard]] std::optional<std::string>
        find_prefixed_diagnostic_value(const std::vector<std::string>& diagnostics,
                                       const std::string_view prefix) {
            for (const auto& diagnostic : diagnostics) {
                if (diagnostic.rfind(prefix, 0) == 0 && diagnostic.size() > prefix.size()) {
                    return diagnostic.substr(prefix.size());
                }
            }

            return std::nullopt;
        }

        /**
         * @brief Find the value of the latest diagnostic with a specific prefix.
         *
         * @param diagnostics The list of diagnostics.
         * @param prefix The prefix to search for.
         * @return std::optional<std::string> The value of the latest diagnostic if found.
         */
        [[nodiscard]] std::optional<std::string>
        find_latest_prefixed_diagnostic_value(const std::vector<std::string>& diagnostics,
                                              const std::string_view prefix) {
            for (auto it = diagnostics.rbegin(); it != diagnostics.rend(); ++it) {
                if (it->rfind(prefix, 0) == 0 && it->size() > prefix.size()) {
                    return it->substr(prefix.size());
                }
            }

            return std::nullopt;
        }

        /**
         * @brief Summary of LLM usage telemetry.
         */
        struct LlmUsageTelemetrySummary {
            std::optional<std::uint32_t> prompt_tokens;
            std::optional<std::uint32_t> completion_tokens;
            std::optional<double> total_cost;
            std::optional<std::string> finish_reason;
        };

        /**
         * @brief Parse an optional uint32_t value from a diagnostic string.
         * 
         * @param value The diagnostic string to parse.
         * @return std::optional<std::uint32_t> The parsed uint32_t value if successful.
         */
        [[nodiscard]] std::optional<std::uint32_t>
        parse_optional_uint32(const std::optional<std::string>& value) {
            if (!value.has_value()) {
                return std::nullopt;
            }

            try {
                return static_cast<std::uint32_t>(std::stoul(*value));
            } catch (...) {
                return std::nullopt;
            }
        }

        /**
         * @brief Parse an optional double value from a diagnostic string.
         * 
         * @param value The diagnostic string to parse.
         * @return std::optional<double> The parsed double value if successful.
         */
        [[nodiscard]] std::optional<double>
        parse_optional_double(const std::optional<std::string>& value) {
            if (!value.has_value()) {
                return std::nullopt;
            }

            try {
                const double parsed_value = std::stod(*value);
                return parsed_value >= 0.0 ? std::optional<double>(parsed_value) : std::nullopt;
            } catch (...) {
                return std::nullopt;
            }
        }

        /**
         * @brief Summarize LLM usage telemetry from a list of diagnostics.
         * 
         * @param diagnostics The list of diagnostics.
         * @return LlmUsageTelemetrySummary The summarized LLM usage telemetry.
         */
        [[nodiscard]] LlmUsageTelemetrySummary
        summarize_llm_usage_telemetry(const std::vector<std::string>& diagnostics) {
            return LlmUsageTelemetrySummary{
                .prompt_tokens = parse_optional_uint32(
                    find_latest_prefixed_diagnostic_value(diagnostics, "llm_usage.prompt_tokens:")),
                .completion_tokens = parse_optional_uint32(find_latest_prefixed_diagnostic_value(
                    diagnostics, "llm_usage.completion_tokens:")),
                .total_cost = parse_optional_double(
                    find_latest_prefixed_diagnostic_value(diagnostics, "llm_usage.total_cost:")),
                .finish_reason =
                    find_latest_prefixed_diagnostic_value(diagnostics, "llm_usage.finish_reason:"),
            };
        }

        /**
         * @brief Emit a decision bridge checkpoint with the given telemetry and request information.
         * 
         * @param telemetry The cognition telemetry object.
         * @param request The cognition step request.
         * @param step The current step in the decision process.
         * @param outcome The outcome of the decision step.
         * @param fallback_allowed Whether fallback is allowed.
         * @param failure_code The failure code, if any.
         * @param result_code The result code, if any.
         * @param error_info The error information, if any.
         * @param elapsed_ms The elapsed time in milliseconds, if available.
         * @param deadline_ms The deadline time in milliseconds, if available.
         * @param diagnostic_count The number of diagnostics, if available.
         * @param resolved_route The resolved route, if available.
         * @param failure_category The failure category, if available.
         */
        void emit_decision_bridge_checkpoint(
            const observability::CognitionTelemetry& telemetry, const CognitionStepRequest& request,
            const std::string& step, const std::string& outcome, const bool fallback_allowed,
            const std::string& failure_code,
            std::optional<contracts::ResultCode> result_code = std::nullopt,
            const contracts::ErrorInfo* error_info = nullptr,
            std::optional<std::uint32_t> elapsed_ms = std::nullopt,
            std::optional<std::uint32_t> deadline_ms = std::nullopt,
            std::size_t diagnostic_count = 0U,
            std::optional<std::string> resolved_route = std::nullopt,
            std::optional<std::string> failure_category = std::nullopt) {
            std::vector<TelemetryField> fields;
            fields.reserve(10U);
            append_detail_field(fields, "source", "llm_bridge");
            append_detail_field(fields, "fallback_allowed", fallback_allowed ? "true" : "false");
            if (resolved_route.has_value()) {
                append_detail_field(fields, "resolved_route", *resolved_route);
            }
            if (failure_category.has_value()) {
                append_detail_field(fields, "failure_category", *failure_category);
            }
            if (!failure_code.empty()) {
                append_detail_field(fields, "structured_projection_failure_code", failure_code);
            }
            if (elapsed_ms.has_value()) {
                append_detail_field(fields, "elapsed_ms", std::to_string(*elapsed_ms));
            }
            if (deadline_ms.has_value()) {
                append_detail_field(fields, "deadline_ms", std::to_string(*deadline_ms));
            }
            append_detail_field(fields, "diagnostic_count",
                                std::to_string(static_cast<unsigned long long>(diagnostic_count)));
            if (error_info != nullptr) {
                append_error_info_fields(fields, *error_info);
            }

            emit_pipeline_checkpoint(
                telemetry,
                make_stage_context(request, "execution", outcome == "degraded", result_code),
                "decision", step, outcome, std::move(fields));
        }

        /**
         * @brief Emit a reflection bridge checkpoint with the given telemetry and request information.
         * 
         * @param telemetry The cognition telemetry object.
         * @param request The reflection request object.
         * @param outcome The outcome of the reflection step.
         * @param fallback_allowed Whether fallback is allowed.
         * @param failure_code The failure code, if any.
         * @param result_code The result code, if any.
         * @param error_info The error information, if any.
         * @param elapsed_ms The elapsed time in milliseconds, if available.
         * @param deadline_ms The deadline time in milliseconds, if available.
         * @param diagnostic_count The number of diagnostics, if available.
         * @param resolved_route The resolved route, if available.
         * @param failure_category The failure category, if available.
         */
        void emit_reflection_bridge_checkpoint(
            const observability::CognitionTelemetry& telemetry, const ReflectionRequest& request,
            const std::string& outcome, const bool fallback_allowed,
            const std::string& failure_code,
            std::optional<contracts::ResultCode> result_code = std::nullopt,
            const contracts::ErrorInfo* error_info = nullptr,
            std::optional<std::uint32_t> elapsed_ms = std::nullopt,
            std::optional<std::uint32_t> deadline_ms = std::nullopt,
            std::size_t diagnostic_count = 0U,
            std::optional<std::string> resolved_route = std::nullopt,
            std::optional<std::string> failure_category = std::nullopt) {
            std::vector<TelemetryField> fields;
            fields.reserve(10U);
            append_detail_field(fields, "source", "llm_bridge");
            append_detail_field(fields, "fallback_allowed", fallback_allowed ? "true" : "false");
            if (resolved_route.has_value()) {
                append_detail_field(fields, "resolved_route", *resolved_route);
            }
            if (failure_category.has_value()) {
                append_detail_field(fields, "failure_category", *failure_category);
            }
            if (!failure_code.empty()) {
                append_detail_field(fields, "structured_projection_failure_code", failure_code);
            }
            if (elapsed_ms.has_value()) {
                append_detail_field(fields, "elapsed_ms", std::to_string(*elapsed_ms));
            }
            if (deadline_ms.has_value()) {
                append_detail_field(fields, "deadline_ms", std::to_string(*deadline_ms));
            }
            append_detail_field(fields, "diagnostic_count",
                                std::to_string(static_cast<unsigned long long>(diagnostic_count)));
            if (error_info != nullptr) {
                append_error_info_fields(fields, *error_info);
            }

            emit_pipeline_checkpoint(
                telemetry,
                make_stage_context(request, "reflection", outcome == "degraded", result_code),
                "reflection", "reflection", outcome, std::move(fields));
        }

        /**
         * @brief Create an ErrorInfo object with the given result code, stage, message, and source component.
         * 
         * @param result_code The result code.
         * @param stage The stage where the error occurred.
         * @param message The error message.
         * @param source_component The source component of the error.
         * @return contracts::ErrorInfo The constructed ErrorInfo object.
         */
        [[nodiscard]] contracts::ErrorInfo make_error_info(contracts::ResultCode result_code,
                                                           std::string stage, std::string message,
                                                           std::string source_component) {
            contracts::ErrorInfo error_info;
            error_info.failure_type = contracts::classify_result_code(result_code);
            error_info.retryable = false;
            error_info.safe_to_replan = false;
            error_info.details.code = static_cast<int>(result_code);
            error_info.details.stage = std::move(stage);
            error_info.details.message = std::move(message);
            error_info.source_ref.ref_type = "component";
            error_info.source_ref.ref_id = std::move(source_component);
            return error_info;
        }

        /**
         * @brief Create an ErrorInfo object for a stage timeout error.
         * 
         * @param stage The stage where the timeout occurred.
         * @param request_id The request ID associated with the timeout.
         * @param trace_id The trace ID associated with the timeout.
         * @param elapsed_ms The elapsed time in milliseconds before the timeout.
         * @param source_component The source component of the error.
         * @return contracts::ErrorInfo The constructed ErrorInfo object.
         */
        [[nodiscard]] contracts::ErrorInfo
        make_stage_timeout_error_info(std::string stage, const std::string& request_id,
                                      const std::string& trace_id, std::uint32_t elapsed_ms,
                                      std::string source_component) {
            return make_error_info(contracts::ResultCode::RuntimeRetryExhausted, std::move(stage),
                                   std::string{"cognition.stage_timeout request_id="} + request_id +
                                       " trace_id=" + trace_id +
                                       " elapsed_ms=" + std::to_string(elapsed_ms),
                                   std::move(source_component));
        }

        /**
         * @brief Determine if a context reload should be recommended based on the context confidence.
         * 
         * @param context_confidence The confidence level of the current context.
         * @return true If a context reload should be recommended.
         * @return false If a context reload is not necessary.
         */
        [[nodiscard]] bool should_recommend_context_reload(float context_confidence) {
            return context_confidence < 0.45F;
        }

        /**
         * @brief Collect missing evidence references from a perception result.
         * 
         * @param perception_result The perception result containing ambiguities and clarification questions.
         * @return std::vector<std::string> A vector of missing evidence references.
         */
        [[nodiscard]] std::vector<std::string>
        collect_missing_evidence(const perception::PerceptionResult& perception_result) {
            std::vector<std::string> missing_evidence;
            for (const auto& ambiguity : perception_result.ambiguities) {
                append_unique(missing_evidence, ambiguity.missing_evidence_refs);
            }
            for (const auto& clarification_question : perception_result.clarification_questions) {
                append_unique(missing_evidence, clarification_question.evidence_refs);
            }
            return missing_evidence;
        }

        /**
         * @brief Create an ActionDecision object for a clarification fallback.
         * 
         * @param request The CognitionStepRequest object.
         * @param rationale The rationale for the clarification fallback.
         * @return ActionDecision The constructed ActionDecision object.
         */
        [[nodiscard]] ActionDecision
        make_clarification_fallback(const CognitionStepRequest& request, std::string rationale) {
            ActionDecision decision;
            decision.decision_kind = ActionDecisionKind::AskClarification;
            decision.rationale = std::move(rationale);
            decision.confidence = std::max(0.55F, request.belief_state.confidence.value_or(0.35F));
            decision.clarification_needed = true;
            decision.clarification_question =
                "What concrete target or evidence should cognition confirm before continuing?";
            decision.response_outline = decision::ResponseOutline{
                .summary = default_response_summary(request),
                .key_points = {"Await user clarification before executing external actions."},
            };
            decision.candidate_scores.push_back(decision::CandidateDecisionScore{
                .candidate_name = "ask_clarification",
                .score = decision.confidence,
                .rationale = "fallback clarification path retained safety after perception failure",
            });
            return decision;
        }

        /**
         * @brief Create an ActionDecision object for a safe convergence fallback.
         * 
         * @param request The CognitionStepRequest object.
         * @param rationale The rationale for the safe convergence fallback.
         * @return ActionDecision The constructed ActionDecision object.
         */
        [[nodiscard]] ActionDecision
        make_converge_safe_fallback(const CognitionStepRequest& request, std::string rationale) {
            ActionDecision decision;
            decision.decision_kind = ActionDecisionKind::ConvergeSafe;
            decision.rationale = std::move(rationale);
            decision.confidence = std::max(0.60F, request.belief_state.confidence.value_or(0.40F));
            decision.response_outline = decision::ResponseOutline{
                .summary = default_response_summary(request),
                .key_points = {"Return a bounded response without external execution."},
            };
            decision.candidate_scores.push_back(decision::CandidateDecisionScore{
                .candidate_name = "converge_safe",
                .score = decision.confidence,
                .rationale = "fallback safe convergence preserved runtime ownership",
            });
            return decision;
        }

        /**
         * @brief Determine if two perception results disagree.
         * 
         * @param authoritative_perception The authoritative perception result.
         * @param rule_perception The rule-based perception result.
         * @return true If the perception results disagree.
         * @return false If the perception results agree.
         */
        [[nodiscard]] bool
        perception_results_disagree(const perception::PerceptionResult& authoritative_perception,
                                    const perception::PerceptionResult& rule_perception) {
            return authoritative_perception.task_type != rule_perception.task_type ||
                   authoritative_perception.requires_clarification !=
                       rule_perception.requires_clarification;
        }

        /**
         * @brief Apply perception context to a CognitionDecisionResult object.
         * 
         * @param result The CognitionDecisionResult object to update.
         * @param perception_result The perception result containing context information.
         */
        void apply_perception_context(CognitionDecisionResult& result,
                                      const perception::PerceptionResult& perception_result) {
            result.context_sufficiency.context_sufficient =
                !perception_result.requires_clarification;
            result.context_sufficiency.context_confidence = perception_result.confidence;
            result.context_sufficiency.missing_evidence_hints =
                collect_missing_evidence(perception_result);
            result.context_sufficiency.recommend_context_reload =
                perception_result.requires_clarification ||
                should_recommend_context_reload(perception_result.confidence);
        }

        /**
         * @brief Create an ActionDecision object for a perception clarification fallback.
         * 
         * @param request The CognitionStepRequest object.
         * @param perception_result The perception result containing clarification questions.
         * @param rationale The rationale for the perception clarification fallback.
         * @return ActionDecision The constructed ActionDecision object.
         */
        [[nodiscard]] ActionDecision make_perception_clarification_decision(
            const CognitionStepRequest& request,
            const perception::PerceptionResult& perception_result, std::string rationale) {
            auto decision = make_clarification_fallback(request, std::move(rationale));
            if (!perception_result.clarification_questions.empty()) {
                decision.clarification_question =
                    perception_result.clarification_questions.front().question;
            }
            if (decision.response_outline.has_value() &&
                !perception_result.intent_summary.empty()) {
                decision.response_outline->summary = perception_result.intent_summary;
            }
            return decision;
        }

        /**
         * @brief Apply the results of a perception clarification to a CognitionDecisionResult object.
         * 
         * @param result The CognitionDecisionResult object to update.
         * @param request The CognitionStepRequest object.
         * @param perception_result The perception result containing clarification questions.
         * @param rationale The rationale for the perception clarification fallback.
         * @param diagnostic Diagnostic information to append.
         */
        void
        apply_perception_clarification_result(CognitionDecisionResult& result,
                                              const CognitionStepRequest& request,
                                              const perception::PerceptionResult& perception_result,
                                              std::string rationale, std::string diagnostic) {
            apply_perception_context(result, perception_result);
            result.action_decision = make_perception_clarification_decision(
                request, perception_result, std::move(rationale));
            result.context_sufficiency.context_sufficient = false;
            result.context_sufficiency.recommend_context_reload = true;
            if (result.context_sufficiency.missing_evidence_hints.empty()) {
                append_unique(result.context_sufficiency.missing_evidence_hints,
                              "context_packet.user_turn");
            }

            belief::BeliefUpdateHint belief_update_hint;
            belief_update_hint.missing_evidence_refs =
                result.context_sufficiency.missing_evidence_hints;
            belief_update_hint.confidence_hint = perception_result.confidence;
            belief_update_hint.merge_mode = belief::BeliefMergeMode::Merge;
            result.belief_update_hint = std::move(belief_update_hint);
            append_unique(result.diagnostics, std::move(diagnostic));
        }

        /**
         * @brief Apply the results of an invalid decision to a CognitionDecisionResult object.
         * 
         * @param result The CognitionDecisionResult object to update.
         * @param validation_result The validation result containing error information.
         */
        void apply_invalid_decide_result(CognitionDecisionResult& result,
                                         const InputBoundaryValidationResult& validation_result) {
            const auto result_code = validation_result.error_info.has_value() &&
                                             validation_result.error_info->details.code.has_value()
                                         ? static_cast<contracts::ResultCode>(
                                               validation_result.error_info->details.code.value())
                                         : contracts::ResultCode::ValidationFieldMissing;
            result.result_code = result_code;
            result.error_info = validation_result.error_info;
            result.context_sufficiency.context_sufficient = false;
            result.context_sufficiency.context_confidence = 0.0F;
            result.context_sufficiency.missing_evidence_hints = validation_result.missing_fields;
            result.context_sufficiency.recommend_context_reload =
                result_code == contracts::ResultCode::ValidationFieldMissing;
            result.diagnostics.push_back("invalid_input");
        }

        /**
         * @brief Apply the results of an invalid reflection to a CognitionReflectionResult object.
         * 
         * @param result The CognitionReflectionResult object to update.
         * @param validation_result The validation result containing error information.
         */
        void
        apply_invalid_reflection_result(CognitionReflectionResult& result,
                                        const InputBoundaryValidationResult& validation_result) {
            result.result_code = validation_result.error_info.has_value() &&
                                         validation_result.error_info->details.code.has_value()
                                     ? static_cast<contracts::ResultCode>(
                                           validation_result.error_info->details.code.value())
                                     : contracts::ResultCode::ValidationFieldMissing;
            result.error_info = validation_result.error_info;
            result.diagnostics.push_back("invalid_input");
        }

        /**
         * @brief Apply the results of a decision failure to a CognitionDecisionResult object.
         * 
         * @param result The CognitionDecisionResult object to update.
         * @param result_code The result code indicating the type of failure.
         * @param error_info The error information associated with the failure.
         * @param diagnostic Diagnostic information to append.
         */
        void apply_decision_failure(CognitionDecisionResult& result,
                                    contracts::ResultCode result_code,
                                    contracts::ErrorInfo error_info, std::string diagnostic) {
            result.result_code = result_code;
            result.error_info = std::move(error_info);
            result.context_sufficiency.context_sufficient = false;
            result.context_sufficiency.context_confidence = 0.0F;
            result.context_sufficiency.recommend_context_reload = true;
            append_unique(result.diagnostics, std::move(diagnostic));
        }

        /**
         * @brief Parse the structured payload view from a StageLlmCallResult object.
         * 
         * @param bridge_result The StageLlmCallResult object containing the response.
         * @return std::optional<validation::StructuredPayloadView> The parsed structured payload view, or std::nullopt if parsing fails.
         */
        [[nodiscard]] std::optional<validation::StructuredPayloadView>
        parse_bridge_payload_view(const StageLlmCallResult& bridge_result) {
            if (!bridge_result.response.has_value() ||
                !bridge_result.response->content_payload.has_value()) {
                return std::nullopt;
            }

            return validation::StructuredPayloadView::parse_structured_payload(
                *bridge_result.response->content_payload);
        }

        /**
         * @brief Result of projecting a perception result from a structured payload view.
         */
        struct PerceptionProjectionResult {
            bool ok = false;
            std::optional<perception::PerceptionResult> perception_result;
            std::optional<contracts::ErrorInfo> error_info;
        };

        /**
         * @brief Create an ErrorInfo object for a perception projection error.
         * 
         * @param field_path The path of the field that caused the error.
         * @param message The error message.
         * @return contracts::ErrorInfo The constructed ErrorInfo object.
         */
        [[nodiscard]] contracts::ErrorInfo make_perception_projection_error(std::string field_path,
                                                                            std::string message) {
            return contracts::ErrorInfo{
                .failure_type =
                    contracts::classify_result_code(contracts::ResultCode::ValidationFieldMissing),
                .retryable = false,
                .safe_to_replan = false,
                .details =
                    contracts::ErrorDetails{
                        .code = static_cast<int>(contracts::ResultCode::ValidationFieldMissing),
                        .message = std::move(message),
                        .stage = "perception",
                    },
                .source_ref =
                    contracts::ErrorSourceRefMinimal{
                        .ref_type = "cognition.perception_structured_projector",
                        .ref_id = std::move(field_path),
                    },
            };
        }

        /**
         * @brief Project a perception result from a structured payload view.
         * 
         * @param payload_view The structured payload view to project.
         * @return PerceptionProjectionResult The result of the projection, including any errors.
         */
        [[nodiscard]] PerceptionProjectionResult
        project_perception_result(const validation::StructuredPayloadView& payload_view) {
            PerceptionProjectionResult result;

            // Validate and extract required fields from the structured payload view, returning detailed errors if any fields are missing or of the wrong type.
            const auto intent_summary = payload_view.read_string("intent_summary");
            if (!intent_summary.has_value()) {
                result.error_info = make_perception_projection_error(
                    "intent_summary",
                    "perception structured payload must encode intent_summary as a string");
                return result;
            }

            // Validate and extract the task_type field, which is required and must be a string.
            const auto task_type = payload_view.read_string("task_type");
            if (!task_type.has_value()) {
                result.error_info = make_perception_projection_error(
                    "task_type", "perception structured payload must encode task_type as a string");
                return result;
            }

            // Validate and extract the entities field, which is required and must be a list of objects.
            const auto entities_view = payload_view.read_list("entities");
            if (!entities_view.has_value()) {
                result.error_info = make_perception_projection_error(
                    "entities",
                    "perception structured payload must encode entities as an object list");
                return result;
            }

            //  Validate and extract the constraints_digest field, which is required and must be an object.
            const auto constraints_view = payload_view.read_object("constraints_digest");
            if (!constraints_view.has_value()) {
                result.error_info = make_perception_projection_error(
                    "constraints_digest",
                    "perception structured payload must encode constraints_digest as an object");
                return result;
            }

            // Validate and extract the ambiguities field, which is required and must be a list of objects.
            const auto ambiguities_view = payload_view.read_list("ambiguities");
            if (!ambiguities_view.has_value()) {
                result.error_info = make_perception_projection_error(
                    "ambiguities",
                    "perception structured payload must encode ambiguities as an object list");
                return result;
            }

            // Validate and extract the clarification_questions field, which is required and must be a list of objects.
            const auto clarification_view = payload_view.read_list("clarification_questions");
            if (!clarification_view.has_value()) {
                result.error_info = make_perception_projection_error(
                    "clarification_questions", "perception structured payload must encode "
                                               "clarification_questions as an object list");
                return result;
            }

            // Validate and extract the confidence field, which is required and must be a number.
            const auto confidence = payload_view.read_number("confidence");
            if (!confidence.has_value()) {
                result.error_info = make_perception_projection_error(
                    "confidence",
                    "perception structured payload must encode confidence as a number");
                return result;
            }

            // Validate and extract the requires_clarification field, which is required and must be a boolean.
            const auto requires_clarification = payload_view.read_bool("requires_clarification");
            if (!requires_clarification.has_value()) {
                result.error_info = make_perception_projection_error(
                    "requires_clarification", "perception structured payload must encode "
                                              "requires_clarification as a boolean");
                return result;
            }

            // Helper lambda to project a list of strings from the structured payload view, returning detailed errors 
            // if the field is missing or not a list of strings when present.
            const auto project_string_list =
                [](const validation::StructuredPayloadView& object_view,
                   std::string_view field_path) -> std::optional<std::vector<std::string>> {
                const auto token = object_view.field_token(field_path);
                if (!token.has_value() || token->kind == validation::JsonTokenKind::Null) {
                    return std::vector<std::string>{};
                }

                const auto list_view = object_view.read_list(field_path);
                if (!list_view.has_value()) {
                    return std::nullopt;
                }

                std::vector<std::string> values;
                values.reserve(list_view->size());
                for (std::size_t index = 0; index < list_view->size(); ++index) {
                    const auto value = list_view->read_string(index);
                    if (!value.has_value()) {
                        return std::nullopt;
                    }
                    values.push_back(*value);
                }
                return values;
            };

            perception::PerceptionResult perception_result;
            perception_result.intent_summary = *intent_summary;
            perception_result.task_type = *task_type;
            perception_result.confidence = static_cast<float>(*confidence);
            perception_result.requires_clarification = *requires_clarification;

            // Validate and extract the list of entities, ensuring that each entity encodes the required name, 
            // value and confidence fields, and that evidence_refs is a string list when present.
            perception_result.entities.reserve(entities_view->size());
            for (std::size_t index = 0; index < entities_view->size(); ++index) {
                const auto entity_view = entities_view->read_object(index);
                if (!entity_view.has_value()) {
                    result.error_info = make_perception_projection_error(
                        "entities", "perception entities must remain objects");
                    return result;
                }

                const auto entity_name = entity_view->read_string("name");
                const auto entity_value = entity_view->read_string("value");
                const auto entity_confidence = entity_view->read_number("confidence");
                if (!entity_name.has_value() || !entity_value.has_value() ||
                    !entity_confidence.has_value()) {
                    result.error_info = make_perception_projection_error(
                        "entities",
                        "perception entities must encode name, value and confidence fields");
                    return result;
                }

                auto evidence_refs = project_string_list(*entity_view, "evidence_refs");
                if (!evidence_refs.has_value()) {
                    result.error_info = make_perception_projection_error(
                        "entities.evidence_refs", "perception entities must encode evidence_refs "
                                                  "as a string list when present");
                    return result;
                }

                perception_result.entities.push_back(perception::EntityCandidate{
                    .name = *entity_name,
                    .value = *entity_value,
                    .confidence = static_cast<float>(*entity_confidence),
                    .evidence_refs = std::move(*evidence_refs),
                });
            }

            if (const auto hard_constraints =
                    project_string_list(*constraints_view, "hard_constraints");
                hard_constraints.has_value()) {
                perception_result.constraints_digest.hard_constraints =
                    std::move(*hard_constraints);
            } else {
                result.error_info = make_perception_projection_error(
                    "constraints_digest.hard_constraints",
                    "constraints_digest.hard_constraints must remain a string list when present");
                return result;
            }

            if (const auto soft_constraints =
                    project_string_list(*constraints_view, "soft_constraints");
                soft_constraints.has_value()) {
                perception_result.constraints_digest.soft_constraints =
                    std::move(*soft_constraints);
            } else {
                result.error_info = make_perception_projection_error(
                    "constraints_digest.soft_constraints",
                    "constraints_digest.soft_constraints must remain a string list when present");
                return result;
            }

            if (const auto policy_refs = project_string_list(*constraints_view, "policy_refs");
                policy_refs.has_value()) {
                perception_result.constraints_digest.policy_refs = std::move(*policy_refs);
            } else {
                result.error_info = make_perception_projection_error(
                    "constraints_digest.policy_refs",
                    "constraints_digest.policy_refs must remain a string list when present");
                return result;
            }

            perception_result.ambiguities.reserve(ambiguities_view->size());
            for (std::size_t index = 0; index < ambiguities_view->size(); ++index) {
                const auto ambiguity_view = ambiguities_view->read_object(index);
                if (!ambiguity_view.has_value()) {
                    result.error_info = make_perception_projection_error(
                        "ambiguities", "perception ambiguities must remain objects");
                    return result;
                }

                const auto ambiguity_id = ambiguity_view->read_string("ambiguity_id");
                const auto ambiguity_description = ambiguity_view->read_string("description");
                const auto ambiguity_severity = ambiguity_view->read_number("severity");
                if (!ambiguity_id.has_value() || !ambiguity_description.has_value() ||
                    !ambiguity_severity.has_value()) {
                    result.error_info = make_perception_projection_error(
                        "ambiguities", "perception ambiguities must encode ambiguity_id, "
                                       "description and severity fields");
                    return result;
                }

                auto missing_evidence_refs =
                    project_string_list(*ambiguity_view, "missing_evidence_refs");
                if (!missing_evidence_refs.has_value()) {
                    result.error_info = make_perception_projection_error(
                        "ambiguities.missing_evidence_refs",
                        "perception ambiguities must encode missing_evidence_refs as a string list "
                        "when present");
                    return result;
                }

                perception_result.ambiguities.push_back(perception::AmbiguityMarker{
                    .ambiguity_id = *ambiguity_id,
                    .description = *ambiguity_description,
                    .missing_evidence_refs = std::move(*missing_evidence_refs),
                    .severity = static_cast<float>(*ambiguity_severity),
                });
            }

            perception_result.clarification_questions.reserve(clarification_view->size());
            for (std::size_t index = 0; index < clarification_view->size(); ++index) {
                const auto question_view = clarification_view->read_object(index);
                if (!question_view.has_value()) {
                    result.error_info = make_perception_projection_error(
                        "clarification_questions",
                        "perception clarification_questions must remain objects");
                    return result;
                }

                const auto question_text = question_view->read_string("question");
                const auto question_priority = question_view->read_number("priority");
                if (!question_text.has_value() || !question_priority.has_value()) {
                    result.error_info = make_perception_projection_error(
                        "clarification_questions", "perception clarification_questions must encode "
                                                   "question and priority fields");
                    return result;
                }

                auto evidence_refs = project_string_list(*question_view, "evidence_refs");
                if (!evidence_refs.has_value()) {
                    result.error_info = make_perception_projection_error(
                        "clarification_questions.evidence_refs",
                        "perception clarification_questions must encode evidence_refs as a string "
                        "list when present");
                    return result;
                }

                perception_result.clarification_questions.push_back(
                    perception::ClarificationCandidate{
                        .question = *question_text,
                        .evidence_refs = std::move(*evidence_refs),
                        .priority = static_cast<float>(*question_priority),
                    });
            }

            result.ok = true;
            result.perception_result = std::move(perception_result);
            return result;
        }

        /// @brief Result of projecting a reflection decision from a structured payload view.
        struct ReflectionDecisionProjectionResult {
            bool ok = false;
            std::optional<contracts::ReflectionDecision> reflection_decision;
            std::optional<contracts::ErrorInfo> error_info;
        };

        /**
         * @brief Creates an ErrorInfo object for a reflection projection error.
         * 
         * @param field_path The path of the field that caused the error.
         * @param message A descriptive error message.
         * @return contracts::ErrorInfo The constructed ErrorInfo object.
         */
        [[nodiscard]] contracts::ErrorInfo make_reflection_projection_error(std::string field_path,
                                                                            std::string message) {
            return contracts::ErrorInfo{
                .failure_type =
                    contracts::classify_result_code(contracts::ResultCode::ValidationFieldMissing),
                .retryable = false,
                .safe_to_replan = false,
                .details =
                    contracts::ErrorDetails{
                        .code = static_cast<int>(contracts::ResultCode::ValidationFieldMissing),
                        .message = std::move(message),
                        .stage = "reflection",
                    },
                .source_ref =
                    contracts::ErrorSourceRefMinimal{
                        .ref_type = "cognition.reflection_structured_projector",
                        .ref_id = std::move(field_path),
                    },
            };
        }

        /**
         * @brief Parses a string literal into a ReflectionDecisionKind enum value.
         * 
         * @param literal The string literal representing the reflection decision kind.
         * @return std::optional<contracts::ReflectionDecisionKind> The parsed ReflectionDecisionKind, or std::nullopt if the literal is unrecognized.
         */
        [[nodiscard]] std::optional<contracts::ReflectionDecisionKind>
        parse_reflection_decision_kind(std::string_view literal) {
            if (literal == "Continue") {
                return contracts::ReflectionDecisionKind::Continue;
            }
            if (literal == "RetryStep") {
                return contracts::ReflectionDecisionKind::RetryStep;
            }
            if (literal == "Replan") {
                return contracts::ReflectionDecisionKind::Replan;
            }
            if (literal == "AbortSafe") {
                return contracts::ReflectionDecisionKind::AbortSafe;
            }
            return std::nullopt;
        }

        /**
         * @brief Projects a reflection decision from a structured payload view.
         * 
         * @param payload_view The structured payload view containing the reflection decision data.
         * @return ReflectionDecisionProjectionResult The result of the projection, including any errors encountered.
         */
        [[nodiscard]] ReflectionDecisionProjectionResult
        project_reflection_decision(const validation::StructuredPayloadView& payload_view) {
            ReflectionDecisionProjectionResult result;

            const auto request_id = payload_view.read_string("request_id");
            if (!request_id.has_value()) {
                result.error_info = make_reflection_projection_error(
                    "request_id",
                    "reflection structured payload must encode request_id as a string");
                return result;
            }

            const auto decision_kind_literal = payload_view.read_string("decision_kind");
            if (!decision_kind_literal.has_value()) {
                result.error_info = make_reflection_projection_error(
                    "decision_kind",
                    "reflection structured payload must encode decision_kind as a string");
                return result;
            }

            const auto decision_kind = parse_reflection_decision_kind(*decision_kind_literal);
            if (!decision_kind.has_value()) {
                result.error_info = make_reflection_projection_error(
                    "decision_kind",
                    "reflection structured payload carried an unknown decision_kind literal");
                return result;
            }

            const auto rationale = payload_view.read_string("rationale");
            if (!rationale.has_value()) {
                result.error_info = make_reflection_projection_error(
                    "rationale", "reflection structured payload must encode rationale as a string");
                return result;
            }

            contracts::ReflectionDecision reflection_decision;
            reflection_decision.request_id = *request_id;
            reflection_decision.decision_kind = *decision_kind;
            reflection_decision.rationale = *rationale;

            if (const auto token = payload_view.field_token("goal_id");
                token.has_value() && token->kind != validation::JsonTokenKind::Null) {
                const auto goal_id = payload_view.read_string("goal_id");
                if (!goal_id.has_value()) {
                    result.error_info = make_reflection_projection_error(
                        "goal_id", "reflection structured payload must encode goal_id as a string "
                                   "when present");
                    return result;
                }
                reflection_decision.goal_id = *goal_id;
            }

            if (const auto token = payload_view.field_token("confidence");
                token.has_value() && token->kind != validation::JsonTokenKind::Null) {
                const auto confidence = payload_view.read_number("confidence");
                if (!confidence.has_value()) {
                    result.error_info = make_reflection_projection_error(
                        "confidence", "reflection structured payload must encode confidence as a "
                                      "number when present");
                    return result;
                }
                reflection_decision.confidence = static_cast<float>(*confidence);
            }

            if (const auto token = payload_view.field_token("hint_ref");
                token.has_value() && token->kind != validation::JsonTokenKind::Null) {
                const auto hint_ref = payload_view.read_string("hint_ref");
                if (!hint_ref.has_value()) {
                    result.error_info = make_reflection_projection_error(
                        "hint_ref", "reflection structured payload must encode hint_ref as a "
                                    "string when present");
                    return result;
                }
                reflection_decision.hint_ref = *hint_ref;
            }

            if (const auto token = payload_view.field_token("created_at");
                token.has_value() && token->kind != validation::JsonTokenKind::Null) {
                const auto created_at = payload_view.read_number("created_at");
                if (!created_at.has_value()) {
                    result.error_info = make_reflection_projection_error(
                        "created_at", "reflection structured payload must encode created_at as a "
                                      "number when present");
                    return result;
                }
                reflection_decision.created_at = static_cast<std::int64_t>(*created_at);
            }

            const auto project_string_list =
                [&](std::string_view field_path) -> std::optional<std::vector<std::string>> {
                const auto list_view = payload_view.read_list(field_path);
                if (!list_view.has_value()) {
                    return std::nullopt;
                }

                std::vector<std::string> values;
                values.reserve(list_view->size());
                for (std::size_t index = 0; index < list_view->size(); ++index) {
                    const auto value = list_view->read_string(index);
                    if (!value.has_value()) {
                        return std::nullopt;
                    }
                    values.push_back(*value);
                }
                return values;
            };

            if (const auto token = payload_view.field_token("relevant_observation_refs");
                token.has_value() && token->kind != validation::JsonTokenKind::Null) {
                const auto refs = project_string_list("relevant_observation_refs");
                if (!refs.has_value()) {
                    result.error_info = make_reflection_projection_error(
                        "relevant_observation_refs",
                        "reflection structured payload must encode relevant_observation_refs as a "
                        "string list when present");
                    return result;
                }
                reflection_decision.relevant_observation_refs = std::move(*refs);
            }

            if (const auto token = payload_view.field_token("tags");
                token.has_value() && token->kind != validation::JsonTokenKind::Null) {
                const auto tags = project_string_list("tags");
                if (!tags.has_value()) {
                    result.error_info = make_reflection_projection_error(
                        "tags", "reflection structured payload must encode tags as a string list "
                                "when present");
                    return result;
                }
                reflection_decision.tags = std::move(*tags);
            }

            result.ok = true;
            result.reflection_decision = std::move(reflection_decision);
            return result;
        }

        /**
         * @brief Handles fallback or failure for a structured stage in the decision pipeline.
         * 
         * @param telemetry The telemetry object for logging and monitoring.
         * @param request The cognition step request being processed.
         * @param result The cognition decision result to update.
         * @param fallback_allowed Indicates if fallback is allowed for this stage.
         * @param result_code The result code to apply if the stage fails.
         * @param error_info The error information to apply if the stage fails.
         * @param stage The name of the stage being processed.
         * @param diagnostic A diagnostic message to include in the result.
         * @param failure_code The failure code to apply if the stage fails.
         * @return true If fallback was applied.
         * @return false If the stage failed without fallback.
         */
        [[nodiscard]] bool fallback_or_fail_structured_stage(
            const observability::CognitionTelemetry& telemetry, const CognitionStepRequest& request,
            CognitionDecisionResult& result, bool fallback_allowed,
            contracts::ResultCode result_code, const contracts::ErrorInfo& error_info,
            const std::string& stage, std::string diagnostic, const std::string_view failure_code) {
            append_structured_projection_value(result.diagnostics, "failure_code", stage,
                                               std::string(failure_code));
            if (fallback_allowed) {
                append_unique(result.diagnostics, std::move(diagnostic));
                append_unique(result.diagnostics, "decision_pipeline.degraded");
                append_unique(result.diagnostics,
                              std::string{"structured_projection.local_fallback:"} + stage);
                append_structured_projection_value(result.diagnostics, "source", stage,
                                                   "local_fallback");
                emit_decision_bridge_checkpoint(telemetry, request, stage, "degraded", true,
                                                std::string(failure_code), std::nullopt,
                                                &error_info, std::nullopt, std::nullopt,
                                                result.diagnostics.size());
                return true;
            }

            apply_decision_failure(result, result_code, error_info, std::move(diagnostic));
            emit_decision_bridge_checkpoint(telemetry, request, stage, "failed", false,
                                            std::string(failure_code), result.result_code,
                                            result.error_info.has_value() ? &(*result.error_info)
                                                                          : &error_info,
                                            std::nullopt, std::nullopt, result.diagnostics.size());
            return false;
        }

        /**
         * @brief Determines if fallback is allowed for the current decision step.
         * 
         * @param decision_plan The optional stage execution plan.
         * @param config The cognition configuration.
         * @param request The cognition step request.
         * @return true If fallback is allowed.
         * @return false If fallback is not allowed.
         */
        [[nodiscard]] bool
        resolve_decision_fallback_allowed(const std::optional<StageExecutionPlan>& decision_plan,
                                          const CognitionConfig& config,
                                          const CognitionStepRequest& request) {
            if (decision_plan.has_value()) {
                return decision_plan->rule_fallback_enabled;
            }

            (void)config;
            return request.execution_hints.degraded_path_allowed;
        }

        void apply_reflection_failure(CognitionReflectionResult& result,
                                      contracts::ResultCode result_code,
                                      contracts::ErrorInfo error_info, std::string diagnostic) {
            result.result_code = result_code;
            result.error_info = std::move(error_info);
            append_unique(result.diagnostics, std::move(diagnostic));
        }

        [[nodiscard]] std::string optional_text(const std::optional<std::string>& value) {
            return value.value_or(std::string{});
        }

        [[nodiscard]] const StageModelHint* find_stage_model_hint(const StageExecutionPlan& plan,
                                                                  std::string_view stage_name,
                                                                  std::string_view task_type) {
            for (const auto& hint : plan.stage_model_hints) {
                if (hint.stage_name == stage_name && hint.task_type == task_type) {
                    return &hint;
                }
            }

            return nullptr;
        }

        [[nodiscard]] StageModelHint
        make_bridge_model_hint(std::string stage, std::string task_type,
                               ModelCapabilityTier capability_tier, bool requires_structured_output,
                               std::uint32_t max_output_tokens, std::uint32_t deadline_ms) {
            return StageModelHint{
                .stage_name = std::move(stage),
                .task_type = std::move(task_type),
                .capability_tier = capability_tier,
                .max_output_tokens = max_output_tokens,
                .deadline_ms = deadline_ms,
                .requires_structured_output = requires_structured_output,
                .requires_reasoning_trace = capability_tier == ModelCapabilityTier::Advanced ||
                                            capability_tier == ModelCapabilityTier::ReasoningHeavy,
                .cost_sensitivity = 0.0F,
                .preferred_provider = {},
            };
        }

        [[nodiscard]] std::vector<std::string>
        make_decision_stage_messages(const CognitionStepRequest& request, const std::string& stage,
                                     const std::string& task_type) {
            std::vector<std::string> messages;
            messages.push_back("stage=" + stage + "; task_type=" + task_type);
            messages.push_back("goal=" + optional_text(request.goal_contract.goal_description));
            messages.push_back("context=" + optional_text(request.context_packet.user_turn));
            if (request.context_packet.current_goal_summary.has_value()) {
                messages.push_back("goal_summary=" + *request.context_packet.current_goal_summary);
            }
            if (request.latest_observation.has_value() &&
                request.latest_observation->payload.has_value()) {
                messages.push_back("latest_observation=" + *request.latest_observation->payload);
            }
            return messages;
        }

        [[nodiscard]] std::vector<std::string> make_reflection_stage_messages(
            const ReflectionRequest& request, const std::string& task_type = "failure_analysis",
            const contracts::ReflectionDecision* previous_decision = nullptr) {
            std::vector<std::string> messages;
            messages.push_back("stage=reflection; task_type=" + task_type);
            messages.push_back("goal=" + optional_text(request.goal_contract.goal_description));
            messages.push_back("context=" + optional_text(request.context_packet.user_turn));
            if (request.latest_observation.payload.has_value()) {
                messages.push_back("latest_observation=" + *request.latest_observation.payload);
            }
            if (request.latest_observation.error.has_value()) {
                messages.push_back("latest_error=" +
                                   request.latest_observation.error->details.message);
            }
            if (previous_decision != nullptr) {
                if (previous_decision->decision_kind.has_value()) {
                    messages.push_back(
                        "previous_decision_kind=" +
                        replay_reflection_decision_kind_name(*previous_decision->decision_kind));
                }
                if (previous_decision->rationale.has_value()) {
                    messages.push_back("previous_rationale=" + *previous_decision->rationale);
                }
                if (previous_decision->hint_ref.has_value()) {
                    messages.push_back("previous_hint_ref=" + *previous_decision->hint_ref);
                }
            }
            return messages;
        }

        [[nodiscard]] const contracts::ErrorInfo*
        find_reflection_error_info(const ReflectionRequest& request) {
            if (request.latest_observation.error.has_value()) {
                return &*request.latest_observation.error;
            }

            return nullptr;
        }

        [[nodiscard]] bool reflection_profile_is_budget_capped(const std::string& profile_id) {
            return profile_id == "edge_balanced" || profile_id == "edge_minimal" ||
                   profile_id == "factory_test";
        }

        [[nodiscard]] bool reflection_error_allows_self_refine(const ReflectionRequest& request) {
            const auto* error_info = find_reflection_error_info(request);
            if (error_info == nullptr || !error_info->failure_type.has_value()) {
                return false;
            }

            return *error_info->failure_type != contracts::ResultCodeCategory::Tool &&
                   *error_info->failure_type != contracts::ResultCodeCategory::Policy;
        }

        [[nodiscard]] std::uint32_t resolve_reflection_round_limit(
            const ReflectionRequest& request,
            const std::optional<policy::StageExecutionPlan>& reflection_plan) {
            auto round_limit =
                reflection_plan.has_value() && reflection_plan->reflection_round_limit > 0U
                    ? reflection_plan->reflection_round_limit
                    : 2U;
            if (request.execution_hints.low_latency_preferred ||
                reflection_profile_is_budget_capped(request.profile_id)) {
                round_limit = std::min<std::uint32_t>(round_limit, 1U);
            }

            return std::max<std::uint32_t>(1U, round_limit);
        }

        [[nodiscard]] StageModelHint
        make_reflection_self_refine_hint(const ReflectionRequest& request,
                                         const StageModelHint* stage_model_hint) {
            StageModelHint hint =
                stage_model_hint != nullptr
                    ? *stage_model_hint
                    : make_bridge_model_hint(
                          "reflection", "replan_advice", ModelCapabilityTier::ReasoningHeavy, true,
                          192U, request.execution_hints.low_latency_preferred ? 500U : 900U);
            hint.task_type = "replan_advice";
            hint.max_output_tokens = hint.max_output_tokens == 0U
                                         ? 192U
                                         : std::min<std::uint32_t>(hint.max_output_tokens, 192U);
            hint.deadline_ms = hint.deadline_ms == 0U
                                   ? (request.execution_hints.low_latency_preferred ? 500U : 900U)
                                   : std::max<std::uint32_t>(300U, hint.deadline_ms / 2U);
            hint.cost_sensitivity = std::max(hint.cost_sensitivity, 0.55F);
            return hint;
        }

        void append_bridge_diagnostics(std::vector<std::string>& diagnostics,
                                       const StageLlmCallResult& bridge_result,
                                       const std::string& stage) {
            append_unique(diagnostics, std::string{"llm_bridge.invoked:"} + stage);
            if (bridge_result.error_info.has_value()) {
                append_unique(diagnostics, std::string{"llm_bridge.failed:"} + stage);
            } else {
                append_unique(diagnostics, std::string{"llm_bridge.completed:"} + stage);
            }
            append_unique(diagnostics, bridge_result.diagnostics);
        }

        [[nodiscard]] DecisionTelemetryRecord
        make_completed_record(const ActionDecision& decision,
                              const std::vector<std::string>& diagnostics) {
            const auto llm_usage = summarize_llm_usage_telemetry(diagnostics);
            return DecisionTelemetryRecord{
                .decision_kind = decision.decision_kind,
                .confidence = decision.confidence,
                .candidate_scores = decision.candidate_scores,
                .selected_node_id = decision.selected_node_id,
                .prompt_tokens = llm_usage.prompt_tokens,
                .completion_tokens = llm_usage.completion_tokens,
                .total_cost = llm_usage.total_cost,
                .finish_reason = llm_usage.finish_reason,
                .clarification_needed = decision.clarification_needed,
                .clarification_question = decision.clarification_question,
                .response_summary =
                    decision.response_outline.has_value()
                        ? std::optional<std::string>(decision.response_outline->summary)
                        : std::nullopt,
                .audit_refs = {},
            };
        }

        [[nodiscard]] DecisionTelemetryRecord
        make_reflection_record(const contracts::ReflectionDecision& decision,
                               const std::vector<std::string>& diagnostics) {
            const auto llm_usage = summarize_llm_usage_telemetry(diagnostics);
            return DecisionTelemetryRecord{
                .decision_kind = dasall::cognition::decision::ActionDecisionKind::NoDecision,
                .confidence = 0.0F,
                .candidate_scores = {},
                .selected_node_id = std::nullopt,
                .prompt_tokens = llm_usage.prompt_tokens,
                .completion_tokens = llm_usage.completion_tokens,
                .total_cost = llm_usage.total_cost,
                .finish_reason = llm_usage.finish_reason,
                .clarification_needed = false,
                .clarification_question = std::nullopt,
                .response_summary = decision.rationale,
                .audit_refs = {},
            };
        }

        /// @brief Facade class for the cognition engine, implementing the ICognitionEngine 
        /// interface and orchestrating the various components of the cognition system.
        class CognitionFacade final : public ICognitionEngine {
          public:
            explicit CognitionFacade(CognitionConfig config,
                                     CognitionRuntimeDependencies dependencies = {})
                : config_(config), perception_engine_(config), planner_(config), reasoner_(config),
                  reflection_engine_(config),
                  telemetry_(config, observability::make_live_telemetry_sink(dependencies)),
                  llm_bridge_(dependencies.llm_manager != nullptr
                                  ? std::make_shared<CognitionLlmBridge>(
                                        std::move(dependencies.llm_manager))
                                  : nullptr),
                  policy_snapshot_(std::move(dependencies.policy_snapshot)) {}

            /**
             * @brief Processes a cognition step request and returns the decision result.
             * 
             * @param request The cognition step request to process.
             * @return CognitionDecisionResult The result of the cognition decision.
             */
            [[nodiscard]] CognitionDecisionResult
            decide(const CognitionStepRequest& request) override {
                const auto started_at = std::chrono::steady_clock::now();
                const auto validation_result =
                    validation::InputBoundaryValidator::validate_decide_request(request);
                auto telemetry_context = make_stage_context(request, "execution", false);
                ignore_emit_result(telemetry_.emit_stage_started(telemetry_context));

                if (!validation_result.ok()) {
                    CognitionDecisionResult result;
                    apply_invalid_decide_result(result, validation_result);
                    telemetry_context.result_code = static_cast<int>(*result.result_code);
                    telemetry_context.latency_ms = elapsed_ms_since(started_at);
                    ignore_emit_result(
                        telemetry_.emit_stage_failed(telemetry_context, *result.error_info));
                    return result;
                }

                emit_replay_trace(telemetry_, "replay.trace.decide.request", telemetry_context,
                                  serialize_decide_request(request));

                auto result = run_decision_pipeline(request);
                const auto fallback_used =
                    std::find(result.diagnostics.begin(), result.diagnostics.end(),
                              "decision_pipeline.degraded") != result.diagnostics.end();
                telemetry_context = make_stage_context(
                    request, "execution", fallback_used, result.result_code,
                    summarize_structured_projection_telemetry(result.diagnostics));
                telemetry_context.latency_ms = elapsed_ms_since(started_at);

                emit_replay_trace(telemetry_, "replay.trace.decide.result", telemetry_context,
                                  serialize_decision_result(result));

                if (result.error_info.has_value()) {
                    ignore_emit_result(
                        telemetry_.emit_stage_failed(telemetry_context, *result.error_info));
                    return result;
                }

                if (result.action_decision.has_value()) {
                    const auto record =
                        make_completed_record(*result.action_decision, result.diagnostics);
                    if (result.action_decision->decision_kind ==
                        ActionDecisionKind::AskClarification) {
                        ignore_emit_result(
                            telemetry_.emit_clarification_requested(telemetry_context, record));
                    }
                    ignore_emit_result(telemetry_.emit_stage_completed(telemetry_context, record));
                }

                return result;
            }

            /**
             * @brief Processes a reflection request and returns the reflection result.
             * 
             * @param request The reflection request to process.
             * @return CognitionReflectionResult The result of the reflection.
             */
            [[nodiscard]] CognitionReflectionResult
            reflect(const ReflectionRequest& request) override {
                const auto started_at = std::chrono::steady_clock::now();
                const auto validation_result =
                    validation::InputBoundaryValidator::validate_reflection_request(request);
                auto telemetry_context = make_stage_context(request, "reflection", false);
                ignore_emit_result(telemetry_.emit_stage_started(telemetry_context));

                if (!validation_result.ok()) {
                    CognitionReflectionResult result;
                    apply_invalid_reflection_result(result, validation_result);
                    telemetry_context.result_code = static_cast<int>(*result.result_code);
                    telemetry_context.latency_ms = elapsed_ms_since(started_at);
                    ignore_emit_result(
                        telemetry_.emit_stage_failed(telemetry_context, *result.error_info));
                    return result;
                }

                emit_replay_trace(telemetry_, "replay.trace.reflect.request", telemetry_context,
                                  serialize_reflection_request(request));

                auto result = run_reflection_pipeline(request);
                telemetry_context =
                    make_stage_context(request, "reflection", false, result.result_code);
                telemetry_context.latency_ms = elapsed_ms_since(started_at);

                emit_replay_trace(telemetry_, "replay.trace.reflect.result", telemetry_context,
                                  serialize_reflection_result(result));

                if (result.error_info.has_value()) {
                    ignore_emit_result(
                        telemetry_.emit_stage_failed(telemetry_context, *result.error_info));
                    return result;
                }

                if (result.reflection_decision.has_value()) {
                    ignore_emit_result(telemetry_.emit_stage_completed(
                        telemetry_context,
                        make_reflection_record(*result.reflection_decision, result.diagnostics)));
                }

                return result;
            }

          private:
            /**
             * @brief Processes a cognition step request and returns the decision result.
             * 
             * @param request The cognition step request to process.
             * @return CognitionDecisionResult The result of the cognition decision.
             */
            [[nodiscard]] CognitionDecisionResult
            run_decision_pipeline(const CognitionStepRequest& request) {
                CognitionDecisionResult result;

                std::optional<StageExecutionPlan> decision_plan;
                if (policy_snapshot_ != nullptr) {
                    decision_plan = policy::StagePolicyResolver::resolve_decide_plan(
                        *policy_snapshot_, request);
                    if (!decision_plan.has_value()) {
                        apply_decision_failure(
                            result, contracts::ResultCode::PolicyDenied,
                            make_error_info(
                                contracts::ResultCode::PolicyDenied, "cognition.decide.policy",
                                "runtime policy snapshot could not produce a decision stage plan",
                                "cognition::policy::StagePolicyResolver"),
                            "decision_pipeline.policy_projection_failed");
                        return result;
                    }
                }

                const auto* planning_hint =
                    decision_plan.has_value()
                        ? find_stage_model_hint(*decision_plan, "planning", "plan")
                        : nullptr;
                const auto perception_llm_enabled = decision_plan.has_value()
                                                        ? decision_plan->perception_llm_enabled
                                                        : config_.perception.llm_enabled;
                const auto* perception_hint =
                    (decision_plan.has_value() && perception_llm_enabled)
                        ? find_stage_model_hint(*decision_plan, "perception", "perception")
                        : nullptr;
                const auto* execution_hint =
                    decision_plan.has_value()
                        ? find_stage_model_hint(*decision_plan, "execution", "action_decision")
                        : nullptr;
                if (decision_plan.has_value() &&
                    ((perception_llm_enabled && perception_hint == nullptr) ||
                     planning_hint == nullptr || execution_hint == nullptr)) {
                    apply_decision_failure(
                        result, contracts::ResultCode::PolicyDenied,
                        make_error_info(
                            contracts::ResultCode::PolicyDenied, "cognition.decide.policy",
                            "runtime policy snapshot did not expose the required bridge hints",
                            "cognition::policy::StagePolicyResolver"),
                        "decision_pipeline.policy_hints_missing");
                    return result;
                }

                const auto max_plan_nodes = decision_plan.has_value()
                                                ? decision_plan->max_plan_nodes
                                                : config_.max_plan_nodes;
                const auto max_plan_depth = decision_plan.has_value()
                                                ? decision_plan->max_plan_depth
                                                : config_.max_plan_depth;
                const auto stage_deadline_ms =
                    decision_plan.has_value() ? decision_plan->deadline_ms : 0U;
                const auto rule_fallback_enabled =
                    resolve_decision_fallback_allowed(decision_plan, config_, request);

                emit_pipeline_checkpoint(
                    telemetry_, make_stage_context(request, "execution", false), "decision",
                    "policy_plan", "resolved",
                    {
                        TelemetryField{
                            .key = "source",
                            .value = decision_plan.has_value() ? "runtime_policy" : "config",
                        },
                        TelemetryField{
                            .key = "deadline_ms",
                            .value = std::to_string(stage_deadline_ms),
                        },
                        TelemetryField{
                            .key = "llm_bridge_enabled",
                            .value = llm_bridge_ != nullptr ? "true" : "false",
                        },
                        TelemetryField{
                            .key = "fallback_allowed",
                            .value = rule_fallback_enabled ? "true" : "false",
                        },
                        TelemetryField{
                            .key = "perception_llm_enabled",
                            .value = perception_llm_enabled ? "true" : "false",
                        },
                    });

                const auto perception_result = run_stage_with_deadline(
                    stage_deadline_ms, [perception_engine = perception_engine_, request]() mutable {
                        return perception_engine.perceive(request);
                    });
                if (perception_result.timed_out) {
                    emit_pipeline_checkpoint(
                        telemetry_,
                        make_stage_context(request, "execution", false,
                                           contracts::ResultCode::RuntimeRetryExhausted),
                        "decision", "perception", "timeout",
                        {
                            TelemetryField{.key = "source", .value = "perception_engine"},
                            TelemetryField{
                                .key = "elapsed_ms",
                                .value = std::to_string(perception_result.elapsed_ms),
                            },
                            TelemetryField{
                                .key = "deadline_ms",
                                .value = std::to_string(stage_deadline_ms),
                            },
                        });
                    apply_decision_failure(result, contracts::ResultCode::RuntimeRetryExhausted,
                                           make_stage_timeout_error_info(
                                               "perception", request.request_id, request.trace_id,
                                               perception_result.elapsed_ms,
                                               "cognition::perception::PerceptionEngine"),
                                           "decision_pipeline.stage_timeout:perception");
                    return result;
                }

                std::optional<perception::PerceptionResult> rule_perception;
                if (!perception_result.value->has_value()) {
                    if (!perception_llm_enabled && rule_fallback_enabled) {
                        result.action_decision = make_clarification_fallback(
                            request, "decision pipeline degraded to clarification because "
                                     "perception produced no safe output");
                        result.context_sufficiency.context_sufficient = false;
                        result.context_sufficiency.context_confidence =
                            request.belief_state.confidence.value_or(0.25F);
                        result.context_sufficiency.recommend_context_reload = true;
                        result.context_sufficiency.missing_evidence_hints = {
                            "context_packet.user_turn"};
                        belief::BeliefUpdateHint belief_update_hint;
                        belief_update_hint.missing_evidence_refs = {"context_packet.user_turn"};
                        belief_update_hint.confidence_hint = 0.25F;
                        belief_update_hint.merge_mode = belief::BeliefMergeMode::Merge;
                        result.belief_update_hint = std::move(belief_update_hint);
                        append_unique(result.diagnostics, "decision_pipeline.degraded");
                        append_unique(result.diagnostics,
                                      "decision_pipeline.perception_unavailable");
                        emit_pipeline_checkpoint(
                            telemetry_, make_stage_context(request, "execution", true), "decision",
                            "perception", "degraded",
                            {
                                TelemetryField{.key = "source", .value = "perception_engine"},
                                TelemetryField{
                                    .key = "elapsed_ms",
                                    .value = std::to_string(perception_result.elapsed_ms),
                                },
                                TelemetryField{
                                    .key = "missing_evidence_count",
                                    .value = std::to_string(
                                        result.context_sufficiency.missing_evidence_hints.size()),
                                },
                                TelemetryField{
                                    .key = "diagnostic_count",
                                    .value = std::to_string(result.diagnostics.size()),
                                },
                            });
                        return result;
                    }

                    if (perception_llm_enabled) {
                        append_unique(result.diagnostics,
                                      "decision_pipeline.perception_shadow_unavailable");
                    }

                    if (!perception_llm_enabled) {
                        emit_pipeline_checkpoint(
                            telemetry_,
                            make_stage_context(request, "execution", false,
                                               contracts::ResultCode::RuntimeRetryExhausted),
                            "decision", "perception", "failed",
                            {
                                TelemetryField{.key = "source", .value = "perception_engine"},
                                TelemetryField{
                                    .key = "elapsed_ms",
                                    .value = std::to_string(perception_result.elapsed_ms),
                                },
                            });
                        apply_decision_failure(
                            result, contracts::ResultCode::RuntimeRetryExhausted,
                            make_error_info(
                                contracts::ResultCode::RuntimeRetryExhausted,
                                "cognition.decide.perception",
                                "perception engine could not derive a safe cognition result",
                                "cognition::perception::PerceptionEngine"),
                            "decision_pipeline.perception_unavailable");
                        return result;
                    }
                } else {
                    rule_perception = perception_result.value->value();
                }

                std::optional<perception::PerceptionResult> authoritative_perception =
                    rule_perception;
                bool perception_from_llm = false;
                const auto perception_fallback_allowed =
                    rule_fallback_enabled && rule_perception.has_value();

                if (perception_llm_enabled) {
                    append_structured_projection_flag(result.diagnostics, "enabled", "perception");
                    append_structured_projection_flag(result.diagnostics, "required", "perception");
                    append_structured_projection_value(result.diagnostics, "schema_version",
                                                       "perception", "cognition.perception.v1");

                    const auto perception_bridge_result = consume_decision_bridge_stage(
                        request, "perception", "perception", ModelCapabilityTier::Standard, true,
                        384U, perception_fallback_allowed, result, perception_hint);
                    if (result.error_info.has_value()) {
                        return result;
                    }

                    if (perception_bridge_result.has_value()) {
                        const auto schema_validation = validator_.validate_stage_output(
                            *perception_bridge_result, validation::schema_for_perception_result());
                        append_unique(result.diagnostics, schema_validation.diagnostics);
                        if (!schema_validation.ok) {
                            if (!fallback_or_fail_structured_stage(
                                    telemetry_, request, result, perception_fallback_allowed,
                                    contracts::ResultCode::ValidationFieldMissing,
                                    *schema_validation.error_info, "perception",
                                    "structured_projection.schema_violation:perception",
                                    "schema")) {
                                return result;
                            }
                        } else {
                            append_unique(result.diagnostics,
                                          "structured_projection.bridge_payload_valid:perception");
                            const auto payload_view =
                                parse_bridge_payload_view(*perception_bridge_result);
                            if (!payload_view.has_value()) {
                                if (!fallback_or_fail_structured_stage(
                                        telemetry_, request, result, perception_fallback_allowed,
                                        contracts::ResultCode::ValidationFieldMissing,
                                        make_error_info(
                                            contracts::ResultCode::ValidationFieldMissing,
                                            "perception",
                                            "perception bridge payload could not be reparsed for "
                                            "projection",
                                            "cognition::perception::PerceptionStructuredProjector"),
                                        "perception",
                                        "structured_projection.projection_failed:perception",
                                        "projection")) {
                                    return result;
                                }
                            } else {
                                const auto projected_perception =
                                    project_perception_result(*payload_view);
                                if (!projected_perception.ok ||
                                    !projected_perception.perception_result.has_value()) {
                                    if (!fallback_or_fail_structured_stage(
                                            telemetry_, request, result,
                                            perception_fallback_allowed,
                                            contracts::ResultCode::ValidationFieldMissing,
                                            projected_perception.error_info.value_or(
                                                make_error_info(
                                                    contracts::ResultCode::ValidationFieldMissing,
                                                    "perception",
                                                    "perception structured projector could not "
                                                    "produce a perception result",
                                                    "cognition::perception::"
                                                    "PerceptionStructuredProjector")),
                                            "perception",
                                            "structured_projection.projection_failed:perception",
                                            "projection")) {
                                        return result;
                                    }
                                } else {
                                    const auto perception_validation =
                                        validator_.validate_perception_invariants(
                                            *projected_perception.perception_result);
                                    append_unique(result.diagnostics,
                                                  perception_validation.diagnostics);
                                    if (!perception_validation.ok) {
                                        if (!fallback_or_fail_structured_stage(
                                                telemetry_, request, result,
                                                perception_fallback_allowed,
                                                contracts::ResultCode::ValidationFieldMissing,
                                                *perception_validation.error_info, "perception",
                                                "structured_projection.invariant_failed:perception",
                                                "invariant")) {
                                            return result;
                                        }
                                    } else {
                                        authoritative_perception =
                                            *projected_perception.perception_result;
                                        perception_from_llm = true;
                                        append_unique(
                                            result.diagnostics,
                                            "structured_projection.projected_perception_result");
                                        append_structured_projection_value(result.diagnostics,
                                                                           "source", "perception",
                                                                           "llm_bridge");
                                        append_structured_projection_value(
                                            result.diagnostics, "projected_entity_count",
                                            "perception",
                                            std::to_string(
                                                authoritative_perception->entities.size()));
                                    }
                                }
                            }
                        }
                    }
                }

                if (!authoritative_perception.has_value()) {
                    return result;
                }

                if (perception_from_llm) {
                    append_unique(result.diagnostics, authoritative_perception->diagnostics);
                } else if (rule_perception.has_value()) {
                    append_unique(result.diagnostics, rule_perception->diagnostics);
                }

                const auto& perception = *authoritative_perception;

                apply_perception_context(result, perception);

                const auto perception_source =
                    perception_from_llm ? std::string{"llm_bridge"}
                                        : find_structured_projection_value(result.diagnostics,
                                                                           "source", "perception")
                                              .value_or(std::string{"perception_engine"});

                if (perception_from_llm && perception.requires_clarification) {
                    apply_perception_clarification_result(
                        result, request, perception,
                        "decision pipeline requested clarification because perception required "
                        "more evidence before planning",
                        "decision_pipeline.perception_clarification_required");
                    emit_pipeline_checkpoint(
                        telemetry_, make_stage_context(request, "execution", false), "decision",
                        "perception", "clarification",
                        {
                            TelemetryField{.key = "source", .value = perception_source},
                            TelemetryField{
                                .key = "elapsed_ms",
                                .value = std::to_string(perception_result.elapsed_ms),
                            },
                            TelemetryField{
                                .key = "missing_evidence_count",
                                .value = std::to_string(
                                    result.context_sufficiency.missing_evidence_hints.size()),
                            },
                            TelemetryField{
                                .key = "diagnostic_count",
                                .value = std::to_string(result.diagnostics.size()),
                            },
                        });
                    return result;
                }

                if (perception_from_llm && rule_perception.has_value() &&
                    perception_results_disagree(perception, *rule_perception)) {
                    apply_perception_clarification_result(
                        result, request, perception,
                        "decision pipeline requested clarification because perception llm and rule "
                        "paths disagreed",
                        "decision_pipeline.perception_conflict");
                    emit_pipeline_checkpoint(
                        telemetry_, make_stage_context(request, "execution", false), "decision",
                        "perception", "clarification",
                        {
                            TelemetryField{.key = "source", .value = perception_source},
                            TelemetryField{
                                .key = "elapsed_ms",
                                .value = std::to_string(perception_result.elapsed_ms),
                            },
                            TelemetryField{
                                .key = "missing_evidence_count",
                                .value = std::to_string(
                                    result.context_sufficiency.missing_evidence_hints.size()),
                            },
                            TelemetryField{
                                .key = "diagnostic_count",
                                .value = std::to_string(result.diagnostics.size()),
                            },
                        });
                    return result;
                }

                emit_pipeline_checkpoint(
                    telemetry_,
                    make_stage_context(
                        request, "execution",
                        has_diagnostic(result.diagnostics, "decision_pipeline.degraded")),
                    "decision", "perception",
                    has_diagnostic(result.diagnostics, "decision_pipeline.degraded") ? "degraded"
                                                                                     : "completed",
                    {
                        TelemetryField{.key = "source", .value = perception_source},
                        TelemetryField{
                            .key = "elapsed_ms",
                            .value = std::to_string(perception_result.elapsed_ms),
                        },
                        TelemetryField{
                            .key = "missing_evidence_count",
                            .value = std::to_string(
                                result.context_sufficiency.missing_evidence_hints.size()),
                        },
                        TelemetryField{
                            .key = "diagnostic_count",
                            .value = std::to_string(result.diagnostics.size()),
                        },
                    });

                std::optional<plan::PlanGraph> active_plan_graph;
                append_structured_projection_flag(result.diagnostics, "enabled", "planning");
                append_structured_projection_flag(result.diagnostics, "required", "planning");
                append_structured_projection_value(result.diagnostics, "schema_version", "planning",
                                                   "cognition.plan.v1");
                const auto planning_bridge_result = consume_decision_bridge_stage(
                    request, "planning", "plan", ModelCapabilityTier::Standard, true, 512U,
                    rule_fallback_enabled, result, planning_hint);
                if (result.error_info.has_value()) {
                    return result;
                }

                if (planning_bridge_result.has_value()) {
                    const auto schema_validation = validator_.validate_stage_output(
                        *planning_bridge_result, validation::schema_for_planning_plan());
                    append_unique(result.diagnostics, schema_validation.diagnostics);
                    if (!schema_validation.ok) {
                        if (!fallback_or_fail_structured_stage(
                                telemetry_, request, result, rule_fallback_enabled,
                                contracts::ResultCode::ValidationFieldMissing,
                                *schema_validation.error_info, "planning",
                                "structured_projection.schema_violation:planning", "schema")) {
                            return result;
                        }
                    } else {
                        append_unique(result.diagnostics,
                                      "structured_projection.bridge_payload_valid:planning");
                        const auto payload_view =
                            parse_bridge_payload_view(*planning_bridge_result);
                        if (!payload_view.has_value()) {
                            if (!fallback_or_fail_structured_stage(
                                    telemetry_, request, result, rule_fallback_enabled,
                                    contracts::ResultCode::ValidationFieldMissing,
                                    make_error_info(
                                        contracts::ResultCode::ValidationFieldMissing, "planning",
                                        "planning bridge payload could not be reparsed for "
                                        "projection",
                                        "cognition::projection::PlanGraphStructuredProjector"),
                                    "planning", "structured_projection.projection_failed:planning",
                                    "projection")) {
                                return result;
                            }
                        } else {
                            projection::PlanGraphStructuredProjector plan_graph_projector;
                            const auto projected_plan =
                                plan_graph_projector.project_plan_graph(*payload_view);
                            append_unique(result.diagnostics, projected_plan.diagnostics);
                            if (!projected_plan.ok || !projected_plan.plan_graph.has_value()) {
                                if (!fallback_or_fail_structured_stage(
                                        telemetry_, request, result, rule_fallback_enabled,
                                        contracts::ResultCode::ValidationFieldMissing,
                                        projected_plan.error_info.value_or(make_error_info(
                                            contracts::ResultCode::ValidationFieldMissing,
                                            "planning",
                                            "planning projector could not produce a plan graph",
                                            "cognition::projection::PlanGraphStructuredProjector")),
                                        "planning",
                                        "structured_projection.projection_failed:planning",
                                        "projection")) {
                                    return result;
                                }
                            } else {
                                const auto plan_validation =
                                    validator_.validate_plan_graph_invariants(
                                        *projected_plan.plan_graph, max_plan_nodes, max_plan_depth);
                                append_unique(result.diagnostics, plan_validation.diagnostics);
                                if (!plan_validation.ok) {
                                    if (!fallback_or_fail_structured_stage(
                                            telemetry_, request, result, rule_fallback_enabled,
                                            contracts::ResultCode::ValidationFieldMissing,
                                            *plan_validation.error_info, "planning",
                                            "structured_projection.invariant_failed:planning",
                                            "invariant")) {
                                        return result;
                                    }
                                } else {
                                    active_plan_graph = *projected_plan.plan_graph;
                                    append_unique(result.diagnostics,
                                                  "structured_projection.projected_plan_graph");
                                    append_structured_projection_value(result.diagnostics, "source",
                                                                       "planning", "llm_bridge");
                                    append_structured_projection_value(
                                        result.diagnostics, "projected_node_count", "planning",
                                        std::to_string(active_plan_graph->nodes.size()));
                                }
                            }
                        }
                    }
                }

                if (!active_plan_graph.has_value()) {
                    PlanningRequest planning_request;
                    planning_request.caller_domain = request.caller_domain;
                    planning_request.request_id = request.request_id;
                    planning_request.trace_id = request.trace_id;
                    planning_request.profile_id = request.profile_id;
                    planning_request.goal_contract = request.goal_contract;
                    planning_request.context_packet = request.context_packet;
                    planning_request.belief_state = request.belief_state;
                    planning_request.perception_result = perception;
                    planning_request.budget_context = request.budget_context;
                    planning_request.execution_hints = request.execution_hints;
                    const auto local_plan_graph = run_stage_with_deadline(
                        stage_deadline_ms, [planner = planner_, planning_request]() mutable {
                            return planner.build_plan(planning_request);
                        });
                    if (local_plan_graph.timed_out) {
                        emit_pipeline_checkpoint(
                            telemetry_,
                            make_stage_context(
                                request, "execution",
                                has_diagnostic(result.diagnostics, "decision_pipeline.degraded"),
                                contracts::ResultCode::RuntimeRetryExhausted),
                            "decision", "planning", "timeout",
                            {
                                TelemetryField{.key = "source", .value = "local_planner"},
                                TelemetryField{
                                    .key = "elapsed_ms",
                                    .value = std::to_string(local_plan_graph.elapsed_ms),
                                },
                                TelemetryField{
                                    .key = "deadline_ms",
                                    .value = std::to_string(stage_deadline_ms),
                                },
                            });
                        apply_decision_failure(result, contracts::ResultCode::RuntimeRetryExhausted,
                                               make_stage_timeout_error_info(
                                                   "planning", request.request_id, request.trace_id,
                                                   local_plan_graph.elapsed_ms,
                                                   "cognition::planning::Planner"),
                                               "decision_pipeline.stage_timeout:planning");
                        return result;
                    }

                    const auto plan_validation = validator_.validate_plan_graph_invariants(
                        *local_plan_graph.value, max_plan_nodes, max_plan_depth);
                    if (!plan_validation.ok) {
                        if (rule_fallback_enabled) {
                            result.action_decision = make_clarification_fallback(
                                request, "decision pipeline degraded to clarification because plan "
                                         "invariants failed validation");
                            result.context_sufficiency.context_sufficient = false;
                            result.context_sufficiency.context_confidence =
                                std::min(result.context_sufficiency.context_confidence, 0.35F);
                            result.context_sufficiency.recommend_context_reload = true;
                            append_unique(result.context_sufficiency.missing_evidence_hints,
                                          "active_plan");
                            append_unique(result.diagnostics, "decision_pipeline.degraded");
                            append_unique(result.diagnostics,
                                          "decision_pipeline.plan_validation_failed");
                            emit_pipeline_checkpoint(
                                telemetry_, make_stage_context(request, "execution", true),
                                "decision", "planning", "degraded",
                                {
                                    TelemetryField{.key = "source", .value = "local_planner"},
                                    TelemetryField{
                                        .key = "node_count",
                                        .value =
                                            std::to_string(local_plan_graph.value->nodes.size()),
                                    },
                                    TelemetryField{
                                        .key = "diagnostic_count",
                                        .value = std::to_string(result.diagnostics.size()),
                                    },
                                });
                            return result;
                        }

                        emit_pipeline_checkpoint(
                            telemetry_,
                            make_stage_context(
                                request, "execution",
                                has_diagnostic(result.diagnostics, "decision_pipeline.degraded"),
                                contracts::ResultCode::ValidationFieldMissing),
                            "decision", "planning", "failed",
                            {
                                TelemetryField{.key = "source", .value = "local_planner"},
                                TelemetryField{
                                    .key = "node_count",
                                    .value = std::to_string(local_plan_graph.value->nodes.size()),
                                },
                            });
                        apply_decision_failure(result,
                                               contracts::ResultCode::ValidationFieldMissing,
                                               *plan_validation.error_info,
                                               "decision_pipeline.plan_validation_failed");
                        return result;
                    }

                    active_plan_graph = *local_plan_graph.value;
                }

                emit_pipeline_checkpoint(
                    telemetry_,
                    make_stage_context(
                        request, "execution",
                        has_diagnostic(result.diagnostics, "decision_pipeline.degraded")),
                    "decision", "planning",
                    has_diagnostic(result.diagnostics, "decision_pipeline.degraded") ? "degraded"
                                                                                     : "completed",
                    {
                        TelemetryField{
                            .key = "source",
                            .value = find_structured_projection_value(result.diagnostics, "source",
                                                                      "planning")
                                         .value_or(std::string{"local_planner"}),
                        },
                        TelemetryField{
                            .key = "node_count",
                            .value = std::to_string(active_plan_graph->nodes.size()),
                        },
                        TelemetryField{
                            .key = "diagnostic_count",
                            .value = std::to_string(result.diagnostics.size()),
                        },
                    });

                ReasoningRequest reasoning_request;
                reasoning_request.caller_domain = request.caller_domain;
                reasoning_request.request_id = request.request_id;
                reasoning_request.trace_id = request.trace_id;
                reasoning_request.profile_id = request.profile_id;
                reasoning_request.goal_contract = request.goal_contract;
                reasoning_request.context_packet = request.context_packet;
                reasoning_request.belief_state = request.belief_state;
                reasoning_request.available_tool_descriptors = request.available_tool_descriptors;
                reasoning_request.perception_result = perception;
                reasoning_request.active_plan = *active_plan_graph;
                reasoning_request.latest_observation = request.latest_observation;
                reasoning_request.budget_context = request.budget_context;
                reasoning_request.execution_hints = request.execution_hints;

                std::optional<ActionDecision> resolved_action_decision;
                append_structured_projection_flag(result.diagnostics, "enabled", "execution");
                append_structured_projection_flag(result.diagnostics, "required", "execution");
                append_structured_projection_value(result.diagnostics, "schema_version",
                                                   "execution", "cognition.reasoning.v1");
                const auto execution_bridge_result = consume_decision_bridge_stage(
                    request, "execution", "action_decision", ModelCapabilityTier::Standard, true,
                    256U, rule_fallback_enabled, result, execution_hint);
                if (result.error_info.has_value()) {
                    return result;
                }

                if (execution_bridge_result.has_value()) {
                    const auto schema_validation = validator_.validate_stage_output(
                        *execution_bridge_result,
                        validation::schema_for_execution_action_decision());
                    append_unique(result.diagnostics, schema_validation.diagnostics);
                    if (!schema_validation.ok) {
                        if (!fallback_or_fail_structured_stage(
                                telemetry_, request, result, rule_fallback_enabled,
                                contracts::ResultCode::ValidationFieldMissing,
                                *schema_validation.error_info, "execution",
                                "structured_projection.schema_violation:execution", "schema")) {
                            return result;
                        }
                    } else {
                        append_unique(result.diagnostics,
                                      "structured_projection.bridge_payload_valid:execution");
                        const auto payload_view =
                            parse_bridge_payload_view(*execution_bridge_result);
                        if (!payload_view.has_value()) {
                            if (!fallback_or_fail_structured_stage(
                                    telemetry_, request, result, rule_fallback_enabled,
                                    contracts::ResultCode::ValidationFieldMissing,
                                    make_error_info(
                                        contracts::ResultCode::ValidationFieldMissing, "execution",
                                        "execution bridge payload could not be reparsed for "
                                        "projection",
                                        "cognition::projection::ActionDecisionStructuredProjector"),
                                    "execution",
                                    "structured_projection.projection_failed:execution",
                                    "projection")) {
                                return result;
                            }
                        } else {
                            projection::ActionDecisionStructuredProjector action_decision_projector;
                            const auto projected_action =
                                action_decision_projector.project_action_decision(*payload_view);
                            append_unique(result.diagnostics, projected_action.diagnostics);
                            if (!projected_action.ok ||
                                !projected_action.action_decision.has_value()) {
                                if (!fallback_or_fail_structured_stage(
                                        telemetry_, request, result, rule_fallback_enabled,
                                        contracts::ResultCode::ValidationFieldMissing,
                                        projected_action.error_info.value_or(make_error_info(
                                            contracts::ResultCode::ValidationFieldMissing,
                                            "execution",
                                            "execution projector could not produce an action "
                                            "decision",
                                            "cognition::projection::"
                                            "ActionDecisionStructuredProjector")),
                                        "execution",
                                        "structured_projection.projection_failed:execution",
                                        "projection")) {
                                    return result;
                                }
                            } else {
                                const auto decision_validation =
                                    validator_.validate_action_decision_invariants(
                                        *projected_action.action_decision,
                                        &reasoning_request.active_plan);
                                append_unique(result.diagnostics, decision_validation.diagnostics);
                                if (!decision_validation.ok) {
                                    if (!fallback_or_fail_structured_stage(
                                            telemetry_, request, result, rule_fallback_enabled,
                                            contracts::ResultCode::ValidationFieldMissing,
                                            *decision_validation.error_info, "execution",
                                            "structured_projection.invariant_failed:execution",
                                            "invariant")) {
                                        return result;
                                    }
                                } else {
                                    resolved_action_decision = *projected_action.action_decision;
                                    append_unique(
                                        result.diagnostics,
                                        "structured_projection.projected_action_decision");
                                    append_structured_projection_value(result.diagnostics, "source",
                                                                       "execution", "llm_bridge");
                                    append_structured_projection_value(
                                        result.diagnostics, "projected_candidate_count",
                                        "execution",
                                        std::to_string(
                                            resolved_action_decision->candidate_scores.size()));
                                }
                            }
                        }
                    }
                }

                if (!resolved_action_decision.has_value()) {
                    const auto local_action_decision = run_stage_with_deadline(
                        stage_deadline_ms, [reasoner = reasoner_, reasoning_request]() mutable {
                            return reasoner.decide(reasoning_request);
                        });
                    if (local_action_decision.timed_out) {
                        emit_pipeline_checkpoint(
                            telemetry_,
                            make_stage_context(
                                request, "execution",
                                has_diagnostic(result.diagnostics, "decision_pipeline.degraded"),
                                contracts::ResultCode::RuntimeRetryExhausted),
                            "decision", "execution", "timeout",
                            {
                                TelemetryField{.key = "source", .value = "local_reasoner"},
                                TelemetryField{
                                    .key = "elapsed_ms",
                                    .value = std::to_string(local_action_decision.elapsed_ms),
                                },
                                TelemetryField{
                                    .key = "deadline_ms",
                                    .value = std::to_string(stage_deadline_ms),
                                },
                            });
                        apply_decision_failure(
                            result, contracts::ResultCode::RuntimeRetryExhausted,
                            make_stage_timeout_error_info(
                                "execution", request.request_id, request.trace_id,
                                local_action_decision.elapsed_ms, "cognition::reasoning::Reasoner"),
                            "decision_pipeline.stage_timeout:execution");
                        return result;
                    }

                    const auto decision_validation = validator_.validate_action_decision_invariants(
                        *local_action_decision.value, &reasoning_request.active_plan);
                    if (!decision_validation.ok) {
                        if (rule_fallback_enabled) {
                            resolved_action_decision = make_converge_safe_fallback(
                                request, "decision pipeline converged safe because decision "
                                         "invariants failed validation");
                            append_unique(result.diagnostics, "decision_pipeline.degraded");
                            append_unique(result.diagnostics,
                                          "decision_pipeline.action_validation_failed");
                        } else {
                            emit_pipeline_checkpoint(
                                telemetry_,
                                make_stage_context(request, "execution",
                                                   has_diagnostic(result.diagnostics,
                                                                  "decision_pipeline.degraded"),
                                                   contracts::ResultCode::ValidationFieldMissing),
                                "decision", "execution", "failed",
                                {
                                    TelemetryField{.key = "source", .value = "local_reasoner"},
                                    TelemetryField{
                                        .key = "candidate_count",
                                        .value = std::to_string(
                                            local_action_decision.value->candidate_scores.size()),
                                    },
                                });
                            apply_decision_failure(result,
                                                   contracts::ResultCode::ValidationFieldMissing,
                                                   *decision_validation.error_info,
                                                   "decision_pipeline.action_validation_failed");
                            return result;
                        }
                    } else {
                        resolved_action_decision = *local_action_decision.value;
                    }
                }

                emit_pipeline_checkpoint(
                    telemetry_,
                    make_stage_context(
                        request, "execution",
                        has_diagnostic(result.diagnostics, "decision_pipeline.degraded")),
                    "decision", "execution",
                    has_diagnostic(result.diagnostics, "decision_pipeline.degraded") ? "degraded"
                                                                                     : "completed",
                    {
                        TelemetryField{
                            .key = "source",
                            .value = find_structured_projection_value(result.diagnostics, "source",
                                                                      "execution")
                                         .value_or(std::string{"local_reasoner"}),
                        },
                        TelemetryField{
                            .key = "candidate_count",
                            .value =
                                std::to_string(resolved_action_decision->candidate_scores.size()),
                        },
                        TelemetryField{
                            .key = "diagnostic_count",
                            .value = std::to_string(result.diagnostics.size()),
                        },
                    });

                result.action_decision = *resolved_action_decision;
                result.belief_update_hint = belief_update_synthesizer_.synthesize_from_decide(
                    perception, *result.action_decision, request.latest_observation);
                append_unique(result.diagnostics, "decision_pipeline.completed");
                return result;
            }

            /**
             * @brief Processes a reflection request and returns the reflection result.
             * 
             * @param request The reflection request to process.
             * @return CognitionReflectionResult The result of the reflection.
             */
            [[nodiscard]] CognitionReflectionResult
            run_reflection_pipeline(const ReflectionRequest& request) {
                CognitionReflectionResult result;

                const auto reflection_plan =
                    policy_snapshot_ != nullptr
                        ? policy::StagePolicyResolver::resolve_reflection_plan(*policy_snapshot_,
                                                                               request)
                        : std::optional<StageExecutionPlan>{};
                if (policy_snapshot_ != nullptr && !reflection_plan.has_value()) {
                    apply_reflection_failure(
                        result, contracts::ResultCode::PolicyDenied,
                        make_error_info(
                            contracts::ResultCode::PolicyDenied, "cognition.reflection.policy",
                            "runtime policy snapshot could not produce a reflection stage plan",
                            "cognition::policy::StagePolicyResolver"),
                        "reflection_pipeline.policy_projection_failed");
                    return result;
                }

                const auto* reflection_hint =
                    reflection_plan.has_value()
                        ? find_stage_model_hint(*reflection_plan, "reflection", "failure_analysis")
                        : nullptr;
                const auto stage_deadline_ms =
                    reflection_plan.has_value() ? reflection_plan->deadline_ms : 0U;
                const auto reflection_round_limit =
                    resolve_reflection_round_limit(request, reflection_plan);
                if (reflection_plan.has_value() && reflection_hint == nullptr) {
                    apply_reflection_failure(
                        result, contracts::ResultCode::PolicyDenied,
                        make_error_info(
                            contracts::ResultCode::PolicyDenied, "cognition.reflection.policy",
                            "runtime policy snapshot did not expose the reflection bridge hint",
                            "cognition::policy::StagePolicyResolver"),
                        "reflection_pipeline.policy_hints_missing");
                    return result;
                }

                emit_pipeline_checkpoint(
                    telemetry_, make_stage_context(request, "reflection", false), "reflection",
                    "policy_plan", "resolved",
                    {
                        TelemetryField{
                            .key = "source",
                            .value = reflection_plan.has_value() ? "runtime_policy" : "config",
                        },
                        TelemetryField{
                            .key = "deadline_ms",
                            .value = std::to_string(stage_deadline_ms),
                        },
                        TelemetryField{
                            .key = "llm_bridge_enabled",
                            .value = llm_bridge_ != nullptr ? "true" : "false",
                        },
                        TelemetryField{
                            .key = "fallback_allowed",
                            .value =
                                request.execution_hints.degraded_path_allowed ? "true" : "false",
                        },
                    });

                append_structured_projection_flag(result.diagnostics, "enabled", "reflection");
                append_structured_projection_flag(result.diagnostics, "required", "reflection");
                append_structured_projection_value(result.diagnostics, "schema_version",
                                                   "reflection", "cognition.reflection.v1");

                consume_reflection_bridge_stage(request, result, reflection_hint);
                if (result.error_info.has_value()) {
                    return result;
                }

                if (result.reflection_decision.has_value()) {
                    if (has_diagnostic(result.diagnostics,
                                       "structured_projection.projected_reflection_decision") &&
                        reflection_error_allows_self_refine(request)) {
                        if (reflection_round_limit > 1U) {
                            append_unique(result.diagnostics,
                                          "reflection_pipeline.self_refine.started");
                            const auto self_refine_hint =
                                make_reflection_self_refine_hint(request, reflection_hint);
                            CognitionReflectionResult self_refine_result;
                            consume_reflection_bridge_stage(request, self_refine_result,
                                                            &self_refine_hint, "replan_advice",
                                                            &(*result.reflection_decision));
                            if (self_refine_result.reflection_decision.has_value() &&
                                !self_refine_result.result_code.has_value() &&
                                !self_refine_result.error_info.has_value()) {
                                result.reflection_decision =
                                    *self_refine_result.reflection_decision;
                                result.belief_update_hint = self_refine_result.belief_update_hint;
                                append_unique(result.diagnostics, self_refine_result.diagnostics);
                                append_unique(result.diagnostics,
                                              "reflection_pipeline.self_refine.completed");
                            } else {
                                append_unique(
                                    result.diagnostics,
                                    "reflection_pipeline.self_refine.retained_initial_decision");
                            }
                        } else {
                            append_unique(result.diagnostics,
                                          "reflection_pipeline.self_refine.skipped:budget_cap");
                        }
                    }
                    result.reflection_lesson = synthesize_reflection_lesson_projection(
                        request, *result.reflection_decision);
                    emit_pipeline_checkpoint(
                        telemetry_, make_stage_context(request, "reflection", false), "reflection",
                        "analysis", "completed",
                        {
                            TelemetryField{
                                .key = "source",
                                .value = has_diagnostic(result.diagnostics,
                                                        "reflection_pipeline.self_refine.completed")
                                             ? "llm_bridge_self_refine"
                                             : "llm_bridge"},
                            TelemetryField{.key = "diagnostic_count",
                                           .value = std::to_string(result.diagnostics.size())},
                        });
                    append_unique(result.diagnostics, "reflection_pipeline.completed");
                    return result;
                }

                ReflectionAnalysisRequest analysis_request;
                analysis_request.caller_domain = request.caller_domain;
                analysis_request.request_id = request.request_id;
                analysis_request.trace_id = request.trace_id;
                analysis_request.profile_id = request.profile_id;
                analysis_request.latest_observation = request.latest_observation;
                analysis_request.goal_contract = request.goal_contract;
                analysis_request.belief_state = request.belief_state;
                analysis_request.error_info = request.latest_observation.error;
                analysis_request.active_plan = request.active_plan;
                analysis_request.execution_hints = request.execution_hints;

                const auto reflection_decision = run_stage_with_deadline(
                    stage_deadline_ms,
                    [reflection_engine = reflection_engine_, analysis_request]() mutable {
                        return reflection_engine.analyze(analysis_request);
                    });
                if (reflection_decision.timed_out) {
                    emit_pipeline_checkpoint(
                        telemetry_,
                        make_stage_context(
                            request, "reflection",
                            has_diagnostic(result.diagnostics,
                                           "reflection_pipeline.llm_bridge_degraded:reflection"),
                            contracts::ResultCode::RuntimeRetryExhausted),
                        "reflection", "analysis", "timeout",
                        {
                            TelemetryField{.key = "source", .value = "reflection_engine"},
                            TelemetryField{
                                .key = "elapsed_ms",
                                .value = std::to_string(reflection_decision.elapsed_ms),
                            },
                            TelemetryField{
                                .key = "deadline_ms",
                                .value = std::to_string(stage_deadline_ms),
                            },
                        });
                    apply_reflection_failure(result, contracts::ResultCode::RuntimeRetryExhausted,
                                             make_stage_timeout_error_info(
                                                 "reflection", request.request_id, request.trace_id,
                                                 reflection_decision.elapsed_ms,
                                                 "cognition::reflection::ReflectionEngine"),
                                             "reflection_pipeline.stage_timeout:reflection");
                    return result;
                }

                result.reflection_decision = *reflection_decision.value;
                result.reflection_lesson = synthesize_reflection_lesson_projection(
                    request, *result.reflection_decision);
                result.belief_update_hint = belief_update_synthesizer_.synthesize_from_reflection(
                    *reflection_decision.value, request.belief_state, request.latest_observation);
                emit_pipeline_checkpoint(
                    telemetry_,
                    make_stage_context(
                        request, "reflection",
                        has_diagnostic(result.diagnostics,
                                       "reflection_pipeline.llm_bridge_degraded:reflection")),
                    "reflection", "analysis",
                    has_diagnostic(result.diagnostics,
                                   "reflection_pipeline.llm_bridge_degraded:reflection")
                        ? "degraded"
                        : "completed",
                    {
                        TelemetryField{.key = "source", .value = "reflection_engine"},
                        TelemetryField{
                            .key = "elapsed_ms",
                            .value = std::to_string(reflection_decision.elapsed_ms),
                        },
                        TelemetryField{
                            .key = "diagnostic_count",
                            .value = std::to_string(result.diagnostics.size()),
                        },
                    });
                append_unique(result.diagnostics, "reflection_pipeline.completed");
                return result;
            }

            /**
             * @brief Consumes a decision bridge stage and returns the result.
             * 
             * @param request The cognition step request.
             * @param stage The stage name.
             * @param task_type The task type.
             * @param capability_tier The capability tier.
             * @param requires_structured_output Whether structured output is required.
             * @param max_output_tokens The maximum number of output tokens.
             * @param fallback_allowed Whether fallback is allowed.
             * @param result The cognition decision result.
             * @param stage_model_hint The stage model hint.
             * @return std::optional<StageLlmCallResult> The result of the stage LLM call.
             */
            [[nodiscard]] std::optional<StageLlmCallResult> consume_decision_bridge_stage(
                const CognitionStepRequest& request, const std::string& stage,
                const std::string& task_type, ModelCapabilityTier capability_tier,
                bool requires_structured_output, std::uint32_t max_output_tokens,
                bool fallback_allowed, CognitionDecisionResult& result,
                const StageModelHint* stage_model_hint) const {
                if (!llm_bridge_) {
                    const auto bridge_unavailable_error = make_error_info(
                        contracts::ResultCode::RuntimeRetryExhausted, stage,
                        "structured stage requires llm_bridge but no bridge is available",
                        "cognition::llm_bridge::CognitionLlmBridge");
                    append_unique(result.diagnostics,
                                  std::string{"llm_bridge.unavailable:"} + stage);
                    append_structured_projection_value(result.diagnostics, "failure_code", stage,
                                                       "provider");
                    if (fallback_allowed) {
                        append_unique(result.diagnostics,
                                      std::string{"decision_pipeline.llm_bridge_degraded:"} +
                                          stage);
                        append_unique(result.diagnostics, "decision_pipeline.degraded");
                        append_unique(result.diagnostics,
                                      std::string{"structured_projection.local_fallback:"} + stage);
                        append_structured_projection_value(result.diagnostics, "source", stage,
                                                           "local_fallback");
                        emit_decision_bridge_checkpoint(telemetry_, request, stage, "degraded",
                                                        true, "provider", std::nullopt,
                                                        &bridge_unavailable_error, std::nullopt,
                                                        std::nullopt, result.diagnostics.size());
                    } else {
                        apply_decision_failure(result, contracts::ResultCode::RuntimeRetryExhausted,
                                               bridge_unavailable_error,
                                               std::string{"decision_pipeline.llm_bridge_failed:"} +
                                                   stage);
                        emit_decision_bridge_checkpoint(
                            telemetry_, request, stage, "failed", false, "provider",
                            result.result_code,
                            result.error_info.has_value() ? &(*result.error_info)
                                                          : &bridge_unavailable_error,
                            std::nullopt, std::nullopt, result.diagnostics.size());
                    }
                    return std::nullopt;
                }

                llm_bridge::StageLlmCallRequest bridge_request;
                bridge_request.request_id = request.request_id;
                bridge_request.trace_id = request.trace_id;
                bridge_request.llm_call_id = request.request_id + ":" + stage + ":" + task_type;
                bridge_request.stage_name = stage;
                bridge_request.task_type = task_type;
                bridge_request.messages = make_decision_stage_messages(request, stage, task_type);
                bridge_request.model_hint =
                    stage_model_hint != nullptr
                        ? *stage_model_hint
                        : make_bridge_model_hint(
                              stage, task_type, capability_tier, requires_structured_output,
                              max_output_tokens,
                              request.execution_hints.low_latency_preferred ? 1000U : 2500U);
                bridge_request.budget_context = request.budget_context;
                bridge_request.schema_spec = llm_bridge::StageSchemaSpec{
                    .schema_kind = requires_structured_output
                                       ? llm_bridge::StageSchemaKind::JsonObject
                                       : llm_bridge::StageSchemaKind::Text,
                    .output_schema_ref =
                        requires_structured_output
                            ? std::string{"schema://cognition/"} + stage + "/" + task_type
                            : std::string{},
                    .allow_plain_text_fallback = !requires_structured_output,
                };
                const auto bridge_llm_call_id = bridge_request.llm_call_id;

                const auto bridge_result = run_stage_with_deadline(
                    bridge_request.model_hint.deadline_ms,
                    [llm_bridge = llm_bridge_, bridge_request]() mutable {
                        return llm_bridge->invoke_stage(bridge_request);
                    },
                    [llm_bridge = llm_bridge_, bridge_llm_call_id]() {
                        if (llm_bridge != nullptr && !bridge_llm_call_id.empty()) {
                            static_cast<void>(llm_bridge->abandon_call(bridge_llm_call_id));
                        }
                    });
                if (bridge_result.timed_out) {
                    const auto timeout_error = make_stage_timeout_error_info(
                        stage, request.request_id, request.trace_id, bridge_result.elapsed_ms,
                        "cognition::llm_bridge::CognitionLlmBridge");
                    append_structured_projection_value(result.diagnostics, "failure_code", stage,
                                                       "timeout");
                    if (fallback_allowed) {
                        append_unique(result.diagnostics,
                                      std::string{"decision_pipeline.llm_bridge_degraded:"} +
                                          stage);
                        append_unique(result.diagnostics, "decision_pipeline.degraded");
                        append_unique(result.diagnostics,
                                      std::string{"structured_projection.local_fallback:"} + stage);
                        append_structured_projection_value(result.diagnostics, "source", stage,
                                                           "local_fallback");
                        emit_decision_bridge_checkpoint(
                            telemetry_, request, stage, "degraded", true, "timeout", std::nullopt,
                            &timeout_error, bridge_result.elapsed_ms,
                            bridge_request.model_hint.deadline_ms, result.diagnostics.size());
                        return std::nullopt;
                    }

                    apply_decision_failure(result, contracts::ResultCode::RuntimeRetryExhausted,
                                           timeout_error,
                                           std::string{"decision_pipeline.stage_timeout:"} + stage);
                    emit_decision_bridge_checkpoint(
                        telemetry_, request, stage, "failed", false, "timeout", result.result_code,
                        result.error_info.has_value() ? &(*result.error_info) : &timeout_error,
                        bridge_result.elapsed_ms, bridge_request.model_hint.deadline_ms,
                        result.diagnostics.size());
                    return std::nullopt;
                }

                append_bridge_diagnostics(result.diagnostics, *bridge_result.value, stage);
                if (!bridge_result.value->error_info.has_value()) {
                    emit_replay_trace(telemetry_, "replay.trace.decide.bridge_payload",
                                      make_stage_context(request, stage, false),
                                      serialize_bridge_payload("decide.bridge_payload", stage,
                                                               *bridge_result.value));
                    return std::move(*bridge_result.value);
                }

                append_structured_projection_value(result.diagnostics, "failure_code", stage,
                                                   "provider");

                const auto resolved_route =
                    find_prefixed_diagnostic_value(bridge_result.value->diagnostics, "route:");
                const auto failure_category = find_prefixed_diagnostic_value(
                    bridge_result.value->diagnostics, "llm_failure:");

                if (fallback_allowed) {
                    append_unique(result.diagnostics, "decision_pipeline.degraded");
                    append_unique(result.diagnostics,
                                  std::string{"structured_projection.local_fallback:"} + stage);
                    append_structured_projection_value(result.diagnostics, "source", stage,
                                                       "local_fallback");
                    append_unique(result.diagnostics,
                                  std::string{"decision_pipeline.llm_bridge_degraded:"} + stage);
                    emit_decision_bridge_checkpoint(
                        telemetry_, request, stage, "degraded", true, "provider", std::nullopt,
                        &(*bridge_result.value->error_info), bridge_result.elapsed_ms,
                        bridge_request.model_hint.deadline_ms, result.diagnostics.size(),
                        resolved_route, failure_category);
                    return std::nullopt;
                }

                apply_decision_failure(result,
                                       bridge_result.value->result_code.value_or(
                                           contracts::ResultCode::RuntimeRetryExhausted),
                                       *bridge_result.value->error_info,
                                       std::string{"decision_pipeline.llm_bridge_failed:"} + stage);
                emit_decision_bridge_checkpoint(
                    telemetry_, request, stage, "failed", false, "provider", result.result_code,
                    result.error_info.has_value() ? &(*result.error_info)
                                                  : &(*bridge_result.value->error_info),
                    bridge_result.elapsed_ms, bridge_request.model_hint.deadline_ms,
                    result.diagnostics.size(), resolved_route, failure_category);
                return std::nullopt;
            }

            /**
             * @brief Consumes the reflection bridge stage.
             * 
             * @param request The reflection request.
             * @param result The cognition reflection result.
             * @param stage_model_hint The stage model hint.
             * @param task_type The task type.
             * @param previous_decision The previous reflection decision.
             */
            void consume_reflection_bridge_stage(
                const ReflectionRequest& request, CognitionReflectionResult& result,
                const StageModelHint* stage_model_hint,
                std::string_view task_type = "failure_analysis",
                const contracts::ReflectionDecision* previous_decision = nullptr) const {
                if (!llm_bridge_) {
                    append_unique(result.diagnostics, "llm_bridge.unavailable:reflection");
                    emit_reflection_bridge_checkpoint(
                        telemetry_, request, "degraded",
                        request.execution_hints.degraded_path_allowed, "provider", std::nullopt,
                        nullptr, std::nullopt, std::nullopt, result.diagnostics.size());
                    return;
                }

                llm_bridge::StageLlmCallRequest bridge_request;
                bridge_request.request_id = request.request_id;
                bridge_request.trace_id = request.trace_id;
                bridge_request.llm_call_id =
                    request.request_id + ":reflection:" + std::string(task_type);
                bridge_request.stage_name = "reflection";
                bridge_request.task_type = std::string(task_type);
                bridge_request.messages = make_reflection_stage_messages(
                    request, bridge_request.task_type, previous_decision);
                bridge_request.model_hint =
                    stage_model_hint != nullptr
                        ? *stage_model_hint
                        : make_bridge_model_hint(
                              "reflection", bridge_request.task_type, ModelCapabilityTier::Advanced,
                              true, 384U,
                              request.execution_hints.low_latency_preferred ? 1000U : 2500U);
                bridge_request.schema_spec = llm_bridge::StageSchemaSpec{
                    .schema_kind = llm_bridge::StageSchemaKind::JsonObject,
                    .output_schema_ref = "schema://cognition/reflection/v1",
                    .allow_plain_text_fallback = false,
                };
                const auto bridge_llm_call_id = bridge_request.llm_call_id;

                const auto bridge_result = run_stage_with_deadline(
                    bridge_request.model_hint.deadline_ms,
                    [llm_bridge = llm_bridge_, bridge_request]() mutable {
                        return llm_bridge->invoke_stage(bridge_request);
                    },
                    [llm_bridge = llm_bridge_, bridge_llm_call_id]() {
                        if (llm_bridge != nullptr && !bridge_llm_call_id.empty()) {
                            static_cast<void>(llm_bridge->abandon_call(bridge_llm_call_id));
                        }
                    });
                if (bridge_result.timed_out) {
                    const auto timeout_error = make_stage_timeout_error_info(
                        "reflection", request.request_id, request.trace_id,
                        bridge_result.elapsed_ms, "cognition::llm_bridge::CognitionLlmBridge");
                    apply_reflection_failure(result, contracts::ResultCode::RuntimeRetryExhausted,
                                             timeout_error,
                                             "reflection_pipeline.stage_timeout:reflection");
                    emit_reflection_bridge_checkpoint(
                        telemetry_, request, "failed",
                        request.execution_hints.degraded_path_allowed, "timeout",
                        result.result_code,
                        result.error_info.has_value() ? &(*result.error_info) : &timeout_error,
                        bridge_result.elapsed_ms, bridge_request.model_hint.deadline_ms,
                        result.diagnostics.size());
                    return;
                }

                append_bridge_diagnostics(result.diagnostics, *bridge_result.value, "reflection");
                const auto resolved_route =
                    find_prefixed_diagnostic_value(bridge_result.value->diagnostics, "route:");
                const auto failure_category = find_prefixed_diagnostic_value(
                    bridge_result.value->diagnostics, "llm_failure:");

                if (!bridge_result.value->error_info.has_value()) {
                    emit_replay_trace(telemetry_, "replay.trace.reflect.bridge_payload",
                                      make_stage_context(request, "reflection", false),
                                      serialize_bridge_payload("reflect.bridge_payload",
                                                               "reflection", *bridge_result.value));

                    const auto handle_structured_failure =
                        [&](const std::string& failure_code, const contracts::ErrorInfo& error_info,
                            std::string diagnostic) {
                            append_structured_projection_value(result.diagnostics, "failure_code",
                                                               "reflection", failure_code);
                            if (request.execution_hints.degraded_path_allowed) {
                                append_unique(result.diagnostics, std::move(diagnostic));
                                append_unique(result.diagnostics,
                                              "reflection_pipeline.llm_bridge_degraded:reflection");
                                append_unique(result.diagnostics,
                                              "structured_projection.local_fallback:reflection");
                                append_structured_projection_value(result.diagnostics, "source",
                                                                   "reflection", "local_fallback");
                                emit_reflection_bridge_checkpoint(
                                    telemetry_, request, "degraded", true, failure_code,
                                    std::nullopt, &error_info, bridge_result.elapsed_ms,
                                    bridge_request.model_hint.deadline_ms,
                                    result.diagnostics.size(), resolved_route, failure_category);
                                return;
                            }

                            apply_reflection_failure(result,
                                                     contracts::ResultCode::ValidationFieldMissing,
                                                     error_info, std::move(diagnostic));
                            emit_reflection_bridge_checkpoint(
                                telemetry_, request, "failed", false, failure_code,
                                result.result_code,
                                result.error_info.has_value() ? &(*result.error_info) : &error_info,
                                bridge_result.elapsed_ms, bridge_request.model_hint.deadline_ms,
                                result.diagnostics.size(), resolved_route, failure_category);
                        };

                    const auto schema_validation = validator_.validate_stage_output(
                        *bridge_result.value, validation::schema_for_reflection_decision());
                    append_unique(result.diagnostics, schema_validation.diagnostics);
                    if (!schema_validation.ok) {
                        handle_structured_failure(
                            "schema", *schema_validation.error_info,
                            "structured_projection.schema_violation:reflection");
                        return;
                    }

                    append_unique(result.diagnostics,
                                  "structured_projection.bridge_payload_valid:reflection");
                    const auto payload_view = parse_bridge_payload_view(*bridge_result.value);
                    if (!payload_view.has_value()) {
                        handle_structured_failure(
                            "projection",
                            make_reflection_projection_error(
                                "response.content_payload",
                                "reflection bridge payload could not be reparsed for projection"),
                            "structured_projection.projection_failed:reflection");
                        return;
                    }

                    const auto projected_reflection = project_reflection_decision(*payload_view);
                    if (!projected_reflection.ok ||
                        !projected_reflection.reflection_decision.has_value()) {
                        handle_structured_failure(
                            "projection",
                            projected_reflection.error_info.value_or(
                                make_reflection_projection_error(
                                    "reflection_decision", "reflection structured payload could "
                                                           "not produce a reflection decision")),
                            "structured_projection.projection_failed:reflection");
                        return;
                    }

                    const auto invariant_validation =
                        validator_.validate_reflection_decision_invariants(
                            *projected_reflection.reflection_decision);
                    append_unique(result.diagnostics, invariant_validation.diagnostics);
                    if (!invariant_validation.ok) {
                        handle_structured_failure(
                            "invariant", *invariant_validation.error_info,
                            "structured_projection.invariant_failed:reflection");
                        return;
                    }

                    result.reflection_decision = *projected_reflection.reflection_decision;
                    result.belief_update_hint =
                        belief_update_synthesizer_.synthesize_from_reflection(
                            *projected_reflection.reflection_decision, request.belief_state,
                            request.latest_observation);
                    append_unique(result.diagnostics,
                                  "structured_projection.projected_reflection_decision");
                    append_structured_projection_value(result.diagnostics, "source", "reflection",
                                                       "llm_bridge");
                    emit_reflection_bridge_checkpoint(
                        telemetry_, request, "completed",
                        request.execution_hints.degraded_path_allowed, "", std::nullopt, nullptr,
                        bridge_result.elapsed_ms, bridge_request.model_hint.deadline_ms,
                        result.diagnostics.size(), resolved_route, failure_category);
                    return;
                }

                if (request.execution_hints.degraded_path_allowed) {
                    append_unique(result.diagnostics,
                                  "reflection_pipeline.llm_bridge_degraded:reflection");
                    emit_reflection_bridge_checkpoint(
                        telemetry_, request, "degraded", true, "provider", std::nullopt,
                        &(*bridge_result.value->error_info), bridge_result.elapsed_ms,
                        bridge_request.model_hint.deadline_ms, result.diagnostics.size(),
                        resolved_route, failure_category);
                    return;
                }

                apply_reflection_failure(result,
                                         bridge_result.value->result_code.value_or(
                                             contracts::ResultCode::RuntimeRetryExhausted),
                                         *bridge_result.value->error_info,
                                         "reflection_pipeline.llm_bridge_failed:reflection");
                emit_reflection_bridge_checkpoint(
                    telemetry_, request, "failed", false, "provider", result.result_code,
                    result.error_info.has_value() ? &(*result.error_info)
                                                  : &(*bridge_result.value->error_info),
                    bridge_result.elapsed_ms, bridge_request.model_hint.deadline_ms,
                    result.diagnostics.size(), resolved_route, failure_category);
            }

            CognitionConfig config_;
            perception::PerceptionEngine perception_engine_;
            planning::Planner planner_;
            reasoning::Reasoner reasoner_;
            reflection::ReflectionEngine reflection_engine_;
            belief::BeliefUpdateSynthesizer belief_update_synthesizer_;
            validation::StageOutputValidator validator_;
            observability::CognitionTelemetry telemetry_;
            std::shared_ptr<CognitionLlmBridge> llm_bridge_;
            std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot_;
        };

    } // namespace

    std::unique_ptr<ICognitionEngine> create_cognition_engine(const CognitionConfig& config) {
        return std::make_unique<CognitionFacade>(config, CognitionRuntimeDependencies{});
    }

    std::unique_ptr<ICognitionEngine>
    create_cognition_engine(const CognitionConfig& config,
                            CognitionRuntimeDependencies dependencies) {
        return std::make_unique<CognitionFacade>(config, std::move(dependencies));
    }

    std::unique_ptr<ICognitionEngine>
    create_cognition_engine(const profiles::RuntimePolicySnapshot& snapshot,
                            CognitionRuntimeDependencies dependencies) {
        const auto config = config::CognitionConfigProjector::project_config(snapshot);
        if (!config.has_value()) {
            return nullptr;
        }

        if (dependencies.policy_snapshot == nullptr) {
            dependencies.policy_snapshot =
                std::make_shared<const profiles::RuntimePolicySnapshot>(snapshot);
        }
        return std::make_unique<CognitionFacade>(*config, std::move(dependencies));
    }

} // namespace dasall::cognition
