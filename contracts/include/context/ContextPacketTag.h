#pragma once

namespace dasall::contracts {

// ContextPacketTag represents the stable ContextPacket object as a pure type
// marker. Keeping this tag empty preserves the ADR-driven boundary without
// introducing any disallowed field semantics at WP-01 stage.
struct ContextPacketTag final {};

}  // namespace dasall::contracts
