#!/usr/bin/env python3

import json
import sys
from pathlib import Path


LOG_ARTIFACTS = {
    "main": "logging-main-runtime.log",
    "runtime_tool_positive": "logging-runtime-tool-positive.log",
    "runtime_recovery_positive": "logging-runtime-recovery-positive.log",
    "runtime_recovery_negative": "logging-runtime-recovery-negative.log",
}

EVIDENCE_LEVEL = "local_installed_authoritative"


def require_args() -> tuple[Path, int]:
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: generate_logging_package_proof.py <artifact-dir> <start-ts-ms>"
        )

    artifact_dir = Path(sys.argv[1])
    start_ts_ms = int(sys.argv[2])
    return artifact_dir, start_ts_ms


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def read_log_events(path: Path, start_ts_ms: int) -> list[dict]:
    events: list[dict] = []
    for raw_line in read_text(path).splitlines():
        line = raw_line.strip()
        if not line:
            continue
        event = json.loads(line)
        timestamp = event.get("ts_ms", event.get("ts"))
        if not isinstance(timestamp, int):
            continue
        if timestamp < start_ts_ms:
            continue
        events.append(event)
    return events


def event_attrs(event: dict) -> dict:
    attrs = event.get("attrs")
    return attrs if isinstance(attrs, dict) else {}


def event_message(event: dict) -> str:
    message = event.get("message")
    return message if isinstance(message, str) else ""


def non_empty_correlation(value: object) -> bool:
    return isinstance(value, str) and value not in ("", "unknown", "none")


def unique_event_names(events: list[dict]) -> list[str]:
    names = {event_message(event) for event in events if event_message(event)}
    return sorted(names)


def unique_attr_values(events: list[dict], name: str) -> list[str]:
    values = {
        str(event_attrs(event).get(name))
        for event in events
        if non_empty_correlation(event_attrs(event).get(name))
    }
    return sorted(values)


def correlation_fields_present(events: list[dict], extra_fields: list[str] | None = None) -> dict:
    fields = ["request_id", "session_id", "trace_id"]
    if extra_fields:
        fields.extend(extra_fields)
    return {
        field: any(non_empty_correlation(event_attrs(event).get(field)) for event in events)
        for field in fields
    }


def make_redaction_proof(tokens: list[tuple[str, str]], source_refs: list[str], texts: list[str]) -> dict:
    combined_text = "\n".join(texts)
    checks = []
    ok = True
    for label, token in tokens:
        if not token:
            continue
        absent = token not in combined_text
        ok = ok and absent
        checks.append(
            {
                "label": label,
                "token": token,
                "absent": absent,
            }
        )
    return {
        "ok": ok,
        "source_refs": source_refs,
        "checks": checks,
    }


def query_summary(name: str, events: list[dict], source_logs: list[str]) -> dict:
    return {
        "record_count": len(events),
        "event_names": unique_event_names(events),
        "source_logs": source_logs,
        "query_strategy": "runtime_log_scan",
        "matched_request_ids": unique_attr_values(events, "request_id")[:8],
        "matched_session_ids": unique_attr_values(events, "session_id")[:8],
        "matched_trace_ids": unique_attr_values(events, "trace_id")[:8],
        "evidence_level": EVIDENCE_LEVEL,
    }


def base_summary(
    events: list[dict],
    extra_fields: list[str],
    redaction: dict,
    query_ref: str,
) -> dict:
    return {
        "record_count": len(events),
        "event_names": unique_event_names(events),
        "correlation_fields_present": correlation_fields_present(events, extra_fields),
        "redaction_proof": redaction,
        "query_proof_ref": query_ref,
        "flush_observed": len(events) > 0,
        "evidence_level": EVIDENCE_LEVEL,
    }


def count_message(events: list[dict], message: str) -> int:
    return sum(1 for event in events if event_message(event) == message)


def count_attr_value(events: list[dict], key: str, expected: str) -> int:
    return sum(1 for event in events if event_attrs(event).get(key) == expected)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> None:
    artifact_dir, start_ts_ms = require_args()

    log_paths = {name: artifact_dir / file_name for name, file_name in LOG_ARTIFACTS.items()}
    for name, path in log_paths.items():
        require(path.is_file(), f"missing logging artifact {name}: {path}")

    log_texts = {name: read_text(path) for name, path in log_paths.items()}
    log_events = {name: read_log_events(path, start_ts_ms) for name, path in log_paths.items()}
    all_events = [event for events in log_events.values() for event in events]

    memory_proof = read_json(artifact_dir / "memory-proof.json")
    runtime_installed_proof = read_json(artifact_dir / "runtime-installed-proof.json")
    runtime_proof = read_json(artifact_dir / "runtime-proof.json")
    knowledge_proof = read_json(artifact_dir / "knowledge-proof.json")
    services_installed_proof = read_json(artifact_dir / "services-installed-proof.json")

    module_events = {
        "cognition": [event for event in all_events if event.get("module") == "cognition"],
        "memory": [event for event in all_events if event.get("module") == "memory"],
        "knowledge": [event for event in all_events if event.get("module") == "knowledge"],
        "runtime": [event for event in all_events if event.get("module") == "runtime"],
        "services": [event for event in all_events if event.get("module") == "services"],
    }

    require(module_events["cognition"], "missing cognition logging events in installed runtime logs")
    require(
        count_message(module_events["memory"], "memory writeback.completed") > 0,
        "missing memory writeback.completed event in installed runtime logs",
    )
    require(
        count_message(module_events["knowledge"], "knowledge.retrieve.completed") > 0,
        "missing knowledge.retrieve.completed event in installed runtime logs",
    )
    require(module_events["runtime"], "missing runtime logging events in installed runtime logs")
    require(
        count_message(module_events["services"], "service.data.query.route") > 0,
        "missing service.data.query.route event in installed runtime logs",
    )
    require(
        count_message(module_events["services"], "service.execution.route") > 0,
        "missing service.execution.route event in installed runtime logs",
    )

    services_correlation = correlation_fields_present(
        module_events["services"], ["capability_id", "target_id"]
    )
    require(
        services_correlation["session_id"] and services_correlation["trace_id"],
        "services logging events must preserve session_id and trace_id in installed runtime logs",
    )

    query_proof = {
        "smoke_start_ts_ms": start_ts_ms,
        "subsystems": {
            name: query_summary(name, events, list(LOG_ARTIFACTS.values()))
            for name, events in module_events.items()
        },
    }

    cognition_redaction = make_redaction_proof(
        [
            ("memory_marker", str(memory_proof.get("expected_marker", ""))),
            ("first_prompt", "Remember this exact marker for this session"),
            ("second_prompt", "In this same session, what exact marker did I ask you to remember"),
        ],
        ["run-first.json", "run-second.json"],
        [log_texts["main"]],
    )
    memory_redaction = make_redaction_proof(
        [
            ("memory_marker", str(memory_proof.get("expected_marker", ""))),
            ("latest_summary_text_prefix", str(memory_proof.get("latest_summary_text_prefix", ""))),
        ],
        ["memory-proof.json"],
        [log_texts["main"]],
    )
    knowledge_redaction = make_redaction_proof(
        [
            ("provider_query", "DeepSeek Chat"),
            ("normative_query", "BusinessChainIntegrationMatrix"),
        ],
        ["knowledge-proof.json"],
        [log_texts["main"]],
    )
    runtime_redaction = make_redaction_proof(
        [
            ("tool_checkpoint_ref", str(runtime_installed_proof.get("tool_checkpoint_ref", ""))),
            (
                "recovery_positive_checkpoint_ref",
                str(runtime_installed_proof.get("recovery_positive_checkpoint_ref", "")),
            ),
            ("waiting_checkpoint_ref", str(runtime_installed_proof.get("waiting_checkpoint_ref", ""))),
        ],
        ["runtime-installed-proof.json", "runtime-proof.json"],
        [
            log_texts["runtime_tool_positive"],
            log_texts["runtime_recovery_positive"],
            log_texts["runtime_recovery_negative"],
        ],
    )
    services_redaction = make_redaction_proof(
        [
            ("dataset_filters_json", '"scope":"session"'),
            ("terminal_command", '"command":"echo installed terminal allow"'),
            ("payload_json_key", '"payload_json"'),
        ],
        ["tools-installed-proof.json", "services-installed-proof.json"],
        [log_texts["main"]],
    )

    installed_subsystems = {
        "cognition": base_summary(
            module_events["cognition"],
            [],
            cognition_redaction,
            "logging-query-proof.json#subsystems.cognition",
        ),
        "memory": base_summary(
            module_events["memory"],
            [],
            memory_redaction,
            "logging-query-proof.json#subsystems.memory",
        ),
        "knowledge": base_summary(
            module_events["knowledge"],
            [],
            knowledge_redaction,
            "logging-query-proof.json#subsystems.knowledge",
        ),
        "runtime": base_summary(
            module_events["runtime"],
            [],
            runtime_redaction,
            "logging-query-proof.json#subsystems.runtime",
        ),
        "services": base_summary(
            module_events["services"],
            ["capability_id", "target_id"],
            services_redaction,
            "logging-query-proof.json#subsystems.services",
        ),
    }

    runtime_subsystems = {
        "cognition": {
            **installed_subsystems["cognition"],
            "stage_set": unique_attr_values(module_events["cognition"], "stage"),
            "degraded_event_total": count_message(
                module_events["cognition"], "cognition response.degraded"
            ),
        },
        "memory": {
            **installed_subsystems["memory"],
            "write_back_event_total": count_message(
                module_events["memory"], "memory writeback.completed"
            ),
            "maintenance_event_total": sum(
                1 for event in module_events["memory"] if "maintenance" in event_message(event)
            ),
        },
        "knowledge": {
            **installed_subsystems["knowledge"],
            "telemetry_path_set": unique_attr_values(module_events["knowledge"], "telemetry_path"),
            "fallback_event_total": count_attr_value(
                module_events["knowledge"], "telemetry_path", "fallback"
            ),
        },
        "runtime": {
            **installed_subsystems["runtime"],
            "transition_event_total": count_message(
                module_events["runtime"], "runtime.transition"
            ),
            "audit_pending_event_total": count_attr_value(
                module_events["runtime"], "audit_ref_pending", "true"
            ),
        },
        "services": {
            **installed_subsystems["services"],
            "execution_event_total": count_message(
                module_events["services"], "service.execution.route"
            ),
            "query_event_total": count_message(
                module_events["services"], "service.data.query.route"
            ),
            "catalog_event_total": count_message(
                module_events["services"], "service.data.catalog.route"
            ),
            "request_ledger_replaced": bool(
                services_installed_proof.get("service_bridge_evidence_present")
            ),
        },
    }

    installed_proof = {
        "smoke_start_ts_ms": start_ts_ms,
        "effective_profile_id": runtime_proof.get("effective_profile_id", ""),
        "runtime_log_artifacts": LOG_ARTIFACTS,
        "subsystems": installed_subsystems,
    }
    runtime_proof_summary = {
        "smoke_start_ts_ms": start_ts_ms,
        "effective_profile_id": runtime_proof.get("effective_profile_id", ""),
        "runtime_log_artifacts": LOG_ARTIFACTS,
        "subsystems": runtime_subsystems,
    }

    (artifact_dir / "logging-query-proof.json").write_text(
        json.dumps(query_proof, indent=2, ensure_ascii=True) + "\n",
        encoding="ascii",
    )
    (artifact_dir / "logging-installed-proof.json").write_text(
        json.dumps(installed_proof, indent=2, ensure_ascii=True) + "\n",
        encoding="ascii",
    )
    (artifact_dir / "logging-runtime-proof.json").write_text(
        json.dumps(runtime_proof_summary, indent=2, ensure_ascii=True) + "\n",
        encoding="ascii",
    )


if __name__ == "__main__":
    main()