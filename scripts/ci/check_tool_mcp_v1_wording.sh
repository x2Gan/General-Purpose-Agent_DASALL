#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

owner_docs=(
	"${ROOT_DIR}/docs/architecture/DASALL_tools子系统详细设计.md"
	"${ROOT_DIR}/docs/architecture/DASALL_profiles模块详细设计.md"
)

runtime_policies=(
	"${ROOT_DIR}/profiles/desktop_full/runtime_policy.yaml"
	"${ROOT_DIR}/profiles/cloud_full/runtime_policy.yaml"
	"${ROOT_DIR}/profiles/edge_balanced/runtime_policy.yaml"
	"${ROOT_DIR}/profiles/edge_minimal/runtime_policy.yaml"
	"${ROOT_DIR}/profiles/factory_test/runtime_policy.yaml"
)

check_contains() {
	local file="$1"
	local pattern="$2"
	if ! rg -n --fixed-strings --color never "$pattern" "$file" >/dev/null; then
		echo "[tool-mcp-v1-wording] missing required wording in ${file}: ${pattern}" >&2
		return 1
	fi
}

check_absent_regex() {
	local pattern="$1"
	shift
	if rg -n --color never -i -e "$pattern" "$@"; then
		echo "[tool-mcp-v1-wording] forbidden wording matched: ${pattern}" >&2
		return 1
	fi
}

check_contains \
	"${ROOT_DIR}/docs/architecture/DASALL_tools子系统详细设计.md" \
	"v1 兼容声明固定为 stdio-only"
check_contains \
	"${ROOT_DIR}/docs/architecture/DASALL_profiles模块详细设计.md" \
	"transport selector，v1 transport 固定为 stdio"

for runtime_policy in "${runtime_policies[@]}"; do
	check_contains "$runtime_policy" "MCP v1 transport 固定为 stdio-only。tools_mcp 只控制 MCP 治理通道启停，"
	check_contains "$runtime_policy" "不充当 transport selector；timeout_policy.mcp.* 与 capability_cache_policy.* 也不表达 SSE / streamable-HTTP 选择。"
done

check_absent_regex 'generic MCP ready' "${owner_docs[@]}" "${runtime_policies[@]}"
check_absent_regex 'generic-ready' "${owner_docs[@]}" "${runtime_policies[@]}"
check_absent_regex 'MCP[^[:cntrl:]]*production-ready' "${owner_docs[@]}" "${runtime_policies[@]}"

echo "[tool-mcp-v1-wording] PASS"