#pragma once

#include <array>
#include <cctype>
#include <string_view>

#include "boundary/GuardCommon.h"
#include "event/EventPayload.h"
#include "event/EventType.h"

namespace dasall::contracts {

// EventPayloadBoundaryDecision normalizes payload boundary outcomes.
enum class EventPayloadBoundaryDecision {
  AllowField,
  RejectEnvelopeHeaderAlias,
  RejectEnvelopeCarrierAlias,
};

// EventPayloadGuardResult carries both boolean outcome and stable reason text.
struct EventPayloadGuardResult {
  bool ok = false;
  EventPayloadBoundaryDecision decision = EventPayloadBoundaryDecision::AllowField;
  std::string_view reason = "event payload validation failed";
};

// EventTypeGuardResult provides event-type required/field-rules diagnostics.
struct EventTypeGuardResult {
  bool ok = false;
  std::string_view reason = "event type validation failed";
};

// Frozen EventEnvelopeHeader aliases that must not appear in payload aliases.
inline constexpr std::array<std::string_view, 6>
    kEventPayloadEnvelopeHeaderForbiddenAliases = {
        "event_id",
        "event_type",
        "event_version",
        "occurred_at_ms",
        "request_id",
        "trace_id",
};

// Frozen EventEnvelope carrier aliases that must not appear in payload aliases.
inline constexpr std::array<std::string_view, 1>
    kEventPayloadEnvelopeCarrierForbiddenAliases = {
        "header_keys",
};

// Returns true when the value contains at least one non-whitespace character.
inline bool event_payload_has_non_whitespace_content(std::string_view value) {
  for (const unsigned char ch : value) {
    if (!std::isspace(ch)) {
      return true;
    }
  }

  return false;
}

// Validates EventType required fields defined by WP05-T009 boundary.
inline EventTypeGuardResult validate_event_type_required_fields(const EventType& event_type) {
  if (!has_non_empty_value(event_type.type_key)) {
    return EventTypeGuardResult{
        .ok = false,
        .reason = "type_key is required and must be non-empty",
    };
  }

  if (!event_type.domain.has_value() ||
      *event_type.domain == EventDomain::Unspecified) {
    return EventTypeGuardResult{
        .ok = false,
        .reason = "domain is required and must not be Unspecified",
    };
  }

  if (!event_type.major_version.has_value() || *event_type.major_version == 0U) {
    return EventTypeGuardResult{
        .ok = false,
        .reason = "major_version is required and must be greater than zero",
    };
  }

  return EventTypeGuardResult{
      .ok = true,
      .reason = "all required event type fields present",
  };
}

// Validates EventType field rules on top of required checks.
inline EventTypeGuardResult validate_event_type_field_rules(const EventType& event_type) {
  const auto required_result = validate_event_type_required_fields(event_type);
  if (!required_result.ok) {
    return required_result;
  }

  if (!event_payload_has_non_whitespace_content(*event_type.type_key)) {
    return EventTypeGuardResult{
        .ok = false,
        .reason = "type_key must contain at least one non-whitespace character",
    };
  }

  const int raw_domain = static_cast<int>(*event_type.domain);
  if (raw_domain < static_cast<int>(EventDomain::Checkpoint) ||
      raw_domain > static_cast<int>(EventDomain::Memory)) {
    return EventTypeGuardResult{
        .ok = false,
        .reason = "domain value is outside the known enum range",
    };
  }

  if (event_type.schema_revision.has_value() && *event_type.schema_revision == 0U) {
    return EventTypeGuardResult{
        .ok = false,
        .reason = "schema_revision must be greater than zero when present",
    };
  }

  if (event_type.stability_tier.has_value()) {
    const int raw_tier = static_cast<int>(*event_type.stability_tier);
    if (raw_tier < static_cast<int>(EventStabilityTier::Stable) ||
        raw_tier > static_cast<int>(EventStabilityTier::Internal)) {
      return EventTypeGuardResult{
          .ok = false,
          .reason = "stability_tier value is outside the known enum range",
      };
    }
  }

  return EventTypeGuardResult{
      .ok = true,
      .reason = "event type field rules validation passed",
  };
}

// Validates EventPayload required fields.
inline EventPayloadGuardResult validate_event_payload_required_fields(
    const EventPayload& payload) {
  if (!has_non_empty_value(payload.payload_type)) {
    return EventPayloadGuardResult{
        .ok = false,
        .decision = EventPayloadBoundaryDecision::AllowField,
        .reason = "payload_type is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(payload.payload_json)) {
    return EventPayloadGuardResult{
        .ok = false,
        .decision = EventPayloadBoundaryDecision::AllowField,
        .reason = "payload_json is required and must be non-empty",
    };
  }

  return EventPayloadGuardResult{
      .ok = true,
      .decision = EventPayloadBoundaryDecision::AllowField,
      .reason = "all required event payload fields present",
  };
}

// Evaluates one payload field alias against frozen envelope boundaries.
constexpr EventPayloadGuardResult evaluate_event_payload_forbidden_alias(
    std::string_view field_alias) {
  for (const auto forbidden_alias : kEventPayloadEnvelopeHeaderForbiddenAliases) {
    if (field_alias == forbidden_alias) {
      return EventPayloadGuardResult{
          .ok = false,
          .decision = EventPayloadBoundaryDecision::RejectEnvelopeHeaderAlias,
          .reason = "event payload must not carry EventEnvelope header aliases",
      };
    }
  }

  for (const auto forbidden_alias : kEventPayloadEnvelopeCarrierForbiddenAliases) {
    if (field_alias == forbidden_alias) {
      return EventPayloadGuardResult{
          .ok = false,
          .decision = EventPayloadBoundaryDecision::RejectEnvelopeCarrierAlias,
          .reason = "event payload must not carry EventEnvelope carrier aliases",
      };
    }
  }

  return EventPayloadGuardResult{
      .ok = true,
      .decision = EventPayloadBoundaryDecision::AllowField,
      .reason = "event payload alias is allowed by WP05-T009 boundary",
  };
}

// Validates EventPayload field hygiene and forbidden-alias checks.
inline EventPayloadGuardResult validate_event_payload_field_rules(
    const EventPayload& payload) {
  const auto required_result = validate_event_payload_required_fields(payload);
  if (!required_result.ok) {
    return required_result;
  }

  if (!event_payload_has_non_whitespace_content(*payload.payload_type)) {
    return EventPayloadGuardResult{
        .ok = false,
        .decision = EventPayloadBoundaryDecision::AllowField,
        .reason = "payload_type must contain at least one non-whitespace character",
    };
  }

  if (!event_payload_has_non_whitespace_content(*payload.payload_json)) {
    return EventPayloadGuardResult{
        .ok = false,
        .decision = EventPayloadBoundaryDecision::AllowField,
        .reason = "payload_json must contain at least one non-whitespace character",
    };
  }

  if (payload.schema_ref.has_value() && payload.schema_ref->empty()) {
    return EventPayloadGuardResult{
        .ok = false,
        .decision = EventPayloadBoundaryDecision::AllowField,
        .reason = "schema_ref must be non-empty when present",
    };
  }

  if (payload.producer_module.has_value() && payload.producer_module->empty()) {
    return EventPayloadGuardResult{
        .ok = false,
        .decision = EventPayloadBoundaryDecision::AllowField,
        .reason = "producer_module must be non-empty when present",
    };
  }

  if (payload.payload_version.has_value() && *payload.payload_version == 0U) {
    return EventPayloadGuardResult{
        .ok = false,
        .decision = EventPayloadBoundaryDecision::AllowField,
        .reason = "payload_version must be greater than zero when present",
    };
  }

  if (payload.field_aliases.has_value()) {
    if (payload.field_aliases->empty()) {
      return EventPayloadGuardResult{
          .ok = false,
          .decision = EventPayloadBoundaryDecision::AllowField,
          .reason = "field_aliases must contain at least one item when present",
      };
    }

    for (std::size_t index = 0; index < payload.field_aliases->size(); ++index) {
      const auto& alias = (*payload.field_aliases)[index];
      if (!event_payload_has_non_whitespace_content(alias)) {
        return EventPayloadGuardResult{
            .ok = false,
            .decision = EventPayloadBoundaryDecision::AllowField,
            .reason = "field_aliases must not contain empty or whitespace-only items",
        };
      }

      const auto alias_result = evaluate_event_payload_forbidden_alias(alias);
      if (!alias_result.ok) {
        return alias_result;
      }

      for (std::size_t probe = index + 1; probe < payload.field_aliases->size(); ++probe) {
        if ((*payload.field_aliases)[index] == (*payload.field_aliases)[probe]) {
          return EventPayloadGuardResult{
              .ok = false,
              .decision = EventPayloadBoundaryDecision::AllowField,
              .reason = "field_aliases must not contain duplicate items",
          };
        }
      }
    }
  }

  return EventPayloadGuardResult{
      .ok = true,
      .decision = EventPayloadBoundaryDecision::AllowField,
      .reason = "event payload field rules validation passed",
  };
}

// Boolean helper for callers that only need allow/reject alias outcome.
constexpr bool is_allowed_event_payload_alias(std::string_view field_alias) {
  return evaluate_event_payload_forbidden_alias(field_alias).ok;
}

}  // namespace dasall::contracts
