#include "IKnowledgeService.h"
#include "KnowledgeErrors.h"
#include "KnowledgeTypes.h"

#include <type_traits>

namespace dasall::knowledge {

namespace {

static_assert(std::is_abstract_v<IKnowledgeService>);
static_assert(std::is_enum_v<KnowledgeErrorCode>);

}  // namespace

}  // namespace dasall::knowledge