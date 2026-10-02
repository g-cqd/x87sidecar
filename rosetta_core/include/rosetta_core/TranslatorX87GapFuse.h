#pragma once

#include <cstdint>

struct IRBlock;
struct IRInstr;
struct TranslationResult;

namespace TranslatorX87 {

// fld_gap_fstp -- FLD m32/m64, up to 4 independent non-x87
// instructions, FSTP m32/m64.  See TranslatorX87GapFuse.cpp for the rules.
//
// The copy is performed entirely by the reply for the FLD: the x87 stack is
// never touched (a push followed by a pop is a net no-op), so neither
// reply needs the native<->compact state boundary or the status/tag
// read-modify-write.  The reply for the FSTP then emits nothing.

enum class GapFuseKind : uint8_t {
    kNone,  // translate as usual
    kHead,  // the FLD: emit the whole copy, return insn_idx + 1
    kTail,  // the FSTP whose FLD already emitted the copy: emit nothing
};

struct GapFuse {
    GapFuseKind kind = GapFuseKind::kNone;
    int64_t tail_idx = -1;  // kHead: index of the FSTP
};

// Pure function of the block's IR (plus config) -- no emitted state.  Returns
// the index of the FSTP that heads[idx] pairs with, or -1.
auto match_gap_copy(IRInstr* instrs, int64_t num, int64_t idx) -> int64_t;

// Decide what the request at idx is.  kTail additionally requires that the
// FLD's reply recorded the pairing in the cache, so a FLD stock translated
// itself (fallback, per-block exclusion) is never followed by a lone no-op.
auto classify_gap_fuse(TranslationResult& tr, IRBlock* block, IRInstr* instrs, int64_t num,
                       int64_t idx) -> GapFuse;

// Emit the copy: load the source, convert, store to the FSTP target.
void emit_gap_copy(TranslationResult& tr, IRInstr* fld, IRInstr* fstp);

}  // namespace TranslatorX87
