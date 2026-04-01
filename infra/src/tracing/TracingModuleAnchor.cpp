// Keep tracing public headers in the infra build graph as they are frozen.
#include "tracing/ITracerProvider.h"

namespace dasall::infra::tracing {

// Minimal anchor source to ensure tracing module participates in build graph.
void tracing_module_anchor() {}

}  // namespace dasall::infra::tracing
