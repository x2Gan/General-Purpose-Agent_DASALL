#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

tools_owner_doc="${ROOT_DIR}/docs/architecture/DASALL_tools子系统详细设计.md"
knowledge_owner_doc="${ROOT_DIR}/docs/architecture/DASALL_knowledge子系统详细设计.md"
deliverable_doc="${ROOT_DIR}/docs/todos/tools/deliverables/TOOL-FIX-010-knowledge-search关系收敛.md"

check_contains() {
	local file="$1"
	local pattern="$2"
	if ! rg -n --fixed-strings --color never "$pattern" "$file" >/dev/null; then
		echo "[tools-knowledge-boundary] missing required wording in ${file}: ${pattern}" >&2
		return 1
	fi
}

check_absent_in_sources() {
	local label="$1"
	local pattern="$2"
	shift 2
	if rg -n --color never -e "$pattern" "$@"; then
		echo "[tools-knowledge-boundary] forbidden source coupling matched for ${label}: ${pattern}" >&2
		return 1
	fi
}

check_contains \
	"${tools_owner_doc}" \
	'不得把 `knowledge/*` 当作 tools 实现目录'
check_contains \
	"${tools_owner_doc}" \
	'只有在显式新增 `knowledge.search` builtin、MCP 或 skill binding 时'
check_contains \
	"${knowledge_owner_doc}" \
	"Knowledge retrieval 不会因为同层协作自动成为 tool 暴露面"
check_contains \
	"${deliverable_doc}" \
	'本轮不新增 `knowledge.search` builtin、MCP binding 或 skill binding。'

check_absent_in_sources \
	"knowledge->tools production coupling" \
	'IToolManager|tools::' \
	"${ROOT_DIR}/knowledge"
check_absent_in_sources \
	"tools->knowledge production coupling" \
	'IKnowledgeService|KnowledgeQuery|KnowledgeRetrieveResult|KnowledgeServiceFactory' \
	"${ROOT_DIR}/tools/include" \
	"${ROOT_DIR}/tools/src"
check_absent_in_sources \
	"knowledge.search production exposure" \
	'knowledge\.search' \
	"${ROOT_DIR}/tools/include" \
	"${ROOT_DIR}/tools/src" \
	"${ROOT_DIR}/apps/runtime_support/src"

echo "[tools-knowledge-boundary] PASS"