#include <cstdint>
#include <exception>
#include <iostream>
#include <type_traits>

#include "IKnowledgeService.h"
#include "KnowledgeErrors.h"
#include "KnowledgeTypes.h"

namespace {

using dasall::knowledge::CorpusDescriptor;
using dasall::knowledge::EvidenceBundle;
using dasall::knowledge::EvidenceSlice;
using dasall::knowledge::IKnowledgeService;
using dasall::knowledge::KnowledgeErrorCode;
using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::KnowledgeRetrieveResult;
using dasall::knowledge::RefreshResult;

static_assert(std::is_abstract_v<IKnowledgeService>);
static_assert(std::is_polymorphic_v<IKnowledgeService>);
static_assert(std::is_same_v<std::underlying_type_t<KnowledgeErrorCode>, std::uint16_t>);

static_assert(std::is_empty_v<KnowledgeQuery>);
static_assert(std::is_empty_v<EvidenceSlice>);
static_assert(std::is_empty_v<EvidenceBundle>);
static_assert(std::is_empty_v<CorpusDescriptor>);
static_assert(std::is_empty_v<KnowledgeRetrieveResult>);
static_assert(std::is_empty_v<RefreshResult>);

}  // namespace

int main() {
  try {
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}