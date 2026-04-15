#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::tools {

enum class CapabilityFreshness {
	fresh,
	stale,
	expired,
};

struct CapabilityEntry {
	std::string capability_id;
	std::string capability_version;
	std::vector<std::string> tool_names;
};

struct CapabilitySnapshot {
	std::string server_id;
	std::vector<CapabilityEntry> entries;
	CapabilityFreshness freshness = CapabilityFreshness::expired;
	std::optional<std::int64_t> last_refresh_at_ms;
	std::optional<std::string> last_error;
	std::optional<std::string> trust_marker;
};

class ICapabilityCache {
 public:
	virtual ~ICapabilityCache() = default;

	[[nodiscard]] virtual std::optional<CapabilitySnapshot> snapshot(
			std::string_view server_id) const = 0;

	virtual void update(CapabilitySnapshot snapshot) = 0;
};

}  // namespace dasall::tools