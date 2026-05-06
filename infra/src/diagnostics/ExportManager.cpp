#include "diagnostics/ExportManager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include "diagnostics/DiagnosticsErrors.h"

namespace dasall::infra::diagnostics {
namespace {

constexpr std::string_view kExportManagerSourceRef = "ExportManager";
constexpr std::string_view kLocalTargetPrefix = "local://diagnostics/";
constexpr std::string_view kJsonlSuffix = ".jsonl";

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

[[nodiscard]] std::string current_time_rfc3339_stub() {
  const auto now = std::chrono::system_clock::now();
  const auto now_time = std::chrono::system_clock::to_time_t(now);
  std::tm utc_time{};
  gmtime_r(&now_time, &utc_time);

  std::ostringstream stream;
  stream << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

[[nodiscard]] std::uint32_t rotate_right(std::uint32_t value, std::uint32_t shift) {
  return (value >> shift) | (value << (32U - shift));
}

[[nodiscard]] std::uint32_t sha256_choose(std::uint32_t x,
                                          std::uint32_t y,
                                          std::uint32_t z) {
  return (x & y) ^ (~x & z);
}

[[nodiscard]] std::uint32_t sha256_majority(std::uint32_t x,
                                            std::uint32_t y,
                                            std::uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] std::uint32_t sha256_big_sigma0(std::uint32_t value) {
  return rotate_right(value, 2U) ^ rotate_right(value, 13U) ^ rotate_right(value, 22U);
}

[[nodiscard]] std::uint32_t sha256_big_sigma1(std::uint32_t value) {
  return rotate_right(value, 6U) ^ rotate_right(value, 11U) ^ rotate_right(value, 25U);
}

[[nodiscard]] std::uint32_t sha256_small_sigma0(std::uint32_t value) {
  return rotate_right(value, 7U) ^ rotate_right(value, 18U) ^ (value >> 3U);
}

[[nodiscard]] std::uint32_t sha256_small_sigma1(std::uint32_t value) {
  return rotate_right(value, 17U) ^ rotate_right(value, 19U) ^ (value >> 10U);
}

[[nodiscard]] char hex_digit(std::uint8_t value) {
  return value < 10U ? static_cast<char>('0' + value)
                     : static_cast<char>('a' + (value - 10U));
}

[[nodiscard]] std::string sha256_hex(std::string_view input) {
  std::vector<std::uint8_t> bytes(input.begin(), input.end());
  const std::uint64_t bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;

  bytes.push_back(0x80U);
  while ((bytes.size() % 64U) != 56U) {
    bytes.push_back(0U);
  }

  for (int shift = 56; shift >= 0; shift -= 8) {
    bytes.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));
  }

  std::array<std::uint32_t, 8> state = {
      0x6a09e667U,
      0xbb67ae85U,
      0x3c6ef372U,
      0xa54ff53aU,
      0x510e527fU,
      0x9b05688cU,
      0x1f83d9abU,
      0x5be0cd19U,
  };

  std::array<std::uint32_t, 64> message_schedule{};
  for (std::size_t chunk_offset = 0; chunk_offset < bytes.size(); chunk_offset += 64U) {
    for (std::size_t word_index = 0; word_index < 16U; ++word_index) {
      const std::size_t base_index = chunk_offset + word_index * 4U;
      message_schedule[word_index] = (static_cast<std::uint32_t>(bytes[base_index]) << 24U) |
                                     (static_cast<std::uint32_t>(bytes[base_index + 1U]) << 16U) |
                                     (static_cast<std::uint32_t>(bytes[base_index + 2U]) << 8U) |
                                     static_cast<std::uint32_t>(bytes[base_index + 3U]);
    }

    for (std::size_t word_index = 16U; word_index < message_schedule.size(); ++word_index) {
      message_schedule[word_index] = sha256_small_sigma1(message_schedule[word_index - 2U]) +
                                     message_schedule[word_index - 7U] +
                                     sha256_small_sigma0(message_schedule[word_index - 15U]) +
                                     message_schedule[word_index - 16U];
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];

    for (std::size_t round = 0; round < message_schedule.size(); ++round) {
      const std::uint32_t temp1 =
          h + sha256_big_sigma1(e) + sha256_choose(e, f, g) + kSha256RoundConstants[round] +
          message_schedule[round];
      const std::uint32_t temp2 = sha256_big_sigma0(a) + sha256_majority(a, b, c);

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
  }

  std::string hex;
  hex.reserve(64U);
  for (const std::uint32_t word : state) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      hex.push_back(hex_digit(static_cast<std::uint8_t>((word >> shift) & 0x0fU)));
    }
  }

  return hex;
}

void append_json_string(std::string& output, std::string_view value) {
  output.push_back('"');
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        output += "\\\"";
        break;
      case '\\':
        output += "\\\\";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (character < 0x20U) {
          output += "\\u00";
          output.push_back(hex_digit(static_cast<std::uint8_t>((character >> 4U) & 0x0fU)));
          output.push_back(hex_digit(static_cast<std::uint8_t>(character & 0x0fU)));
        } else {
          output.push_back(static_cast<char>(character));
        }
        break;
    }
  }
  output.push_back('"');
}

void append_json_string_array(std::string& output, const std::vector<std::string>& values) {
  output.push_back('[');
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0U) {
      output.push_back(',');
    }

    append_json_string(output, values[index]);
  }
  output.push_back(']');
}

[[nodiscard]] std::string_view redaction_profile_name(RedactionProfile profile) {
  switch (profile) {
    case RedactionProfile::Strict:
      return "strict";
    case RedactionProfile::Compatibility:
      return "compat";
    case RedactionProfile::Unspecified:
      break;
  }

  return "unspecified";
}

[[nodiscard]] std::string serialize_snapshot_jsonl(const DiagnosticsSnapshot& snapshot) {
  std::string output;
  output.reserve(512U);

  output += "{\"snapshot_id\":";
  append_json_string(output, snapshot.snapshot_id);
  output += ",\"command\":{\"command_id\":";
  append_json_string(output, snapshot.command.command_id);
  output += ",\"command_name\":";
  append_json_string(output, snapshot.command.command_name);
  output += ",\"args\":";
  append_json_string_array(output, snapshot.command.args);
  output += ",\"request_scope\":";
  append_json_string(output, snapshot.command.request_scope);
  output += ",\"timeout_ms\":";
  output += std::to_string(snapshot.command.timeout_ms);
  output += ",\"actor_ref\":";
  append_json_string(output, snapshot.command.actor_ref);
  output += "},\"collected_at\":";
  append_json_string(output, snapshot.collected_at);
  output += ",\"summary\":";
  append_json_string(output, snapshot.summary);
  output += ",\"evidence_refs\":";
  append_json_string_array(output, snapshot.evidence_refs);
  output += ",\"redaction_profile\":";
  append_json_string(output, redaction_profile_name(snapshot.redaction_profile));
  output += ",\"exporter_hint\":";
  append_json_string(output, snapshot.exporter_hint);
  output += "}\n";

  return output;
}

[[nodiscard]] bool is_valid_artifact_name(std::string_view artifact_name) {
  if (artifact_name.empty() || artifact_name.size() > 128U || artifact_name.find("..") != std::string::npos) {
    return false;
  }

  return std::all_of(artifact_name.begin(), artifact_name.end(), [](unsigned char character) {
    return std::isdigit(character) != 0 || (character >= 'a' && character <= 'z') ||
           character == '.' || character == '_' || character == '-';
  });
}

[[nodiscard]] bool is_valid_local_target_ref(std::string_view target_ref) {
  if (target_ref.rfind(kLocalTargetPrefix, 0) != 0 ||
      target_ref.size() <= kLocalTargetPrefix.size() + kJsonlSuffix.size() ||
      target_ref.find('?') != std::string::npos || target_ref.find('#') != std::string::npos ||
      target_ref.rfind(kJsonlSuffix) != target_ref.size() - kJsonlSuffix.size()) {
    return false;
  }

  const std::string_view artifact_name =
      target_ref.substr(kLocalTargetPrefix.size(),
                        target_ref.size() - kLocalTargetPrefix.size() - kJsonlSuffix.size());
  return artifact_name.find('/') == std::string::npos && is_valid_artifact_name(artifact_name);
}

[[nodiscard]] bool is_valid_remote_target_ref(std::string_view target_ref) {
  return target_ref.rfind("https://", 0) == 0 && target_ref.find('@') == std::string::npos &&
         target_ref.find('?') == std::string::npos && target_ref.find('#') == std::string::npos;
}

[[nodiscard]] SnapshotExportResult make_export_failure(DiagnosticsErrorCode code,
                                                       std::string message,
                                                       std::string source_ref) {
  const auto mapping = map_diagnostics_error_code(code);
  return SnapshotExportResult::failure(mapping.result_code,
                                       std::string(diagnostics_error_code_name(code)) + ": " +
                                           std::move(message),
                                       std::string("diagnostics.export_snapshot"),
                                       std::move(source_ref));
}

}  // namespace

ExportManager::ExportManager(ExportManagerOptions options) : options_(std::move(options)) {}

SnapshotExportResult ExportManager::export_snapshot(const DiagnosticsSnapshot& snapshot,
                                                    const SnapshotExportRequest& request) const {
  const std::string source_ref = request.target_ref.empty() ? std::string(kExportManagerSourceRef)
                                                            : request.target_ref;

  if (!snapshot.is_valid()) {
    return make_export_failure(DiagnosticsErrorCode::ExportFail,
                               "diagnostics export requires a valid redacted snapshot",
                               source_ref);
  }

  if (!request.is_valid()) {
    return SnapshotExportResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                         std::string("diagnostics export request must stay fully specified"),
                                         std::string("diagnostics.export_snapshot"),
                                         source_ref);
  }

  if (request.target == ExportTarget::RemoteUpload) {
    if (!options_.remote_export_enabled) {
      return make_export_failure(DiagnosticsErrorCode::RemoteExportDisabled,
                                 "diagnostics remote export is disabled by default",
                                 source_ref);
    }

    if (!is_valid_remote_target_ref(request.target_ref) ||
        std::find(options_.remote_allowed_targets.begin(),
                  options_.remote_allowed_targets.end(),
                  request.target_ref) == options_.remote_allowed_targets.end()) {
      return make_export_failure(DiagnosticsErrorCode::RemoteExportDisabled,
                                 "diagnostics remote export target_ref must match allowed_targets exactly",
                                 source_ref);
    }

    return make_export_failure(DiagnosticsErrorCode::ExportFail,
                               "diagnostics remote export backend is not implemented in the v1 skeleton",
                               source_ref);
  }

  if (!options_.local_export_enabled) {
    return make_export_failure(DiagnosticsErrorCode::ExportFail,
                               "diagnostics local export is disabled",
                               source_ref);
  }

  if (request.target != ExportTarget::LocalFile || request.format != ExportFormat::Json) {
    return make_export_failure(DiagnosticsErrorCode::ExportFail,
                               "diagnostics export skeleton only supports local UTF-8 JSON Lines exports",
                               source_ref);
  }

  if (!is_valid_local_target_ref(request.target_ref)) {
    return make_export_failure(DiagnosticsErrorCode::ExportFail,
                               "diagnostics local export target_ref must match local://diagnostics/<artifact_name>.jsonl",
                               source_ref);
  }

  const std::string payload = serialize_snapshot_jsonl(snapshot);
  return SnapshotExportResult::success(std::string("export-") + snapshot.snapshot_id,
                                       request.target,
                                       request.format,
                                       payload.size(),
                                       std::string("sha256:") + sha256_hex(payload),
                                       current_time_rfc3339_stub());
}

}  // namespace dasall::infra::diagnostics