#pragma once

#include <cstdint>
#include <optional>

struct TranslationResult;
struct IRBlock;
struct IRInstr;

namespace Translator {
auto translate_instruction(TranslationResult* translation_result, IRBlock* block,
                           IRInstr* instr_array, int64_t num_instrs, int64_t insn_idx)
    -> std::optional<int64_t>;

// True when the scratch FPRs stock left free in `tr` cannot hold what a reply
// starting with `opcode` needs (its native-state boundary and the opcode's own
// translator).  translate_instruction then declines before emitting anything
// and stock translates the instruction itself.
auto fpr_pool_too_small(const TranslationResult& tr, uint16_t opcode) -> bool;
};