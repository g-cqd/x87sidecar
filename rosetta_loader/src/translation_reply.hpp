#pragma once

#include <array>
#include <cstdint>

namespace sidecar {

// The stub accepts at most one reserve response per translation request. All
// three buffers are reserved together before retrying the original instruction.
// Fatal refuses unsafe stock fallback for synthetic ARPL or unidentified requests.
enum class ReplyKind : uint64_t { None, Some, Reserve, Fatal };
constexpr uint64_t kMaxReserveBytes = 128 * 1024 * 1024;

struct TranslationReply {
    // Some: next instruction; Reserve: instruction capacity; Fatal: opcode or UINT64_MAX.
    uint64_t result = 0;
    ReplyKind kind = ReplyKind::None;
    std::array<uint64_t, 2> fixup_capacities{};  // external_fixups, _fixups; bytes.
};
static_assert(sizeof(TranslationReply) == 32);

}  // namespace sidecar
