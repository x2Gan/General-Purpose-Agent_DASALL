#include "IKnowledgeService.h"
#include "KnowledgeErrors.h"
#include "KnowledgeTypes.h"

#include <type_traits>

namespace dasall::knowledge {

namespace {

static_assert(std::is_abstract_v<IKnowledgeService>);
static_assert(std::is_enum_v<KnowledgeErrorCode>);
static_assert(std::is_same_v<decltype(&IKnowledgeService::init),
							 bool (IKnowledgeService::*)(const KnowledgeConfigSnapshot&)>);
static_assert(std::is_same_v<decltype(&IKnowledgeService::retrieve),
							 KnowledgeRetrieveResult (IKnowledgeService::*)(
								 const KnowledgeQuery&)>);
static_assert(std::is_same_v<decltype(&IKnowledgeService::request_refresh),
							 RefreshResult (IKnowledgeService::*)(
								 const CorpusChangeSet&)>);

}  // namespace

}  // namespace dasall::knowledge