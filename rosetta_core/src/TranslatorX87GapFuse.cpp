#include "rosetta_core/TranslatorX87GapFuse.h"

#include <cstdint>
#include <cstdlib>

#include "rosetta_core/AssemblerBuffer.h"
#include "rosetta_core/AssemblerHelpers.hpp"
#include "rosetta_core/Config.h"
#include "rosetta_core/CoreConfig.h"
#include "rosetta_core/IRInstr.h"
#include "rosetta_core/IROperand.h"
#include "rosetta_core/Opcode.h"
#include "rosetta_core/Register.h"
#include "rosetta_core/TranslationResult.h"
#include "rosetta_core/TranslatorHelpers.hpp"
#include "rosetta_core/X87Cache.h"

// =============================================================================
// fld_gap_fstp -- float copy through the x87 stack with unrelated non-x87
// instructions in between:
//
//     flds   (%eax)            ; push
//     movss  %xmm1, 0x4(%esi)  ; 0..kGapFuseMaxGap instructions, none x87
//     fstps  0x30(%esi)        ; pop
//
// Every x87 instruction ends a "run", so each of these is translated alone.
// A lone FLD / FSTP pays, besides the data movement, the native<->compact
// state boundary of its reply (f80 <-> f64 conversion of every live stack
// slot, on entry and again on exit) and a status/tag read-modify-write.  The
// pair, however, is a net no-op on the x87 state: the push marks a slot valid
// and writes it, the pop marks it empty again, TOP returns to where it was.
// Only the memory-to-memory copy is observable.
//
// So the FLD's reply does the whole copy and touches no x87 state at all
// (it also skips both boundary conversions), and the FSTP's reply emits
// nothing.  The store thus happens where the FLD was, i.e. BEFORE the gap
// instructions.  That reordering is what the rules below make unobservable.
// With no gap at all (FLD;FSTP adjacent, and nothing x87 after them) the one
// reply consumes both instructions.
//
// Legality (all must hold; anything else leaves both instructions to the
// ordinary paths):
//
//  * FLD m32/m64 (not m80, not ST(i), not FILD) that starts its own run;
//    FSTP m32/m64 (popping, memory form, no segment override) is the FIRST x87
//    instruction after the gap; the gap has at most kGapFuseMaxGap instructions.
//  * Each gap instruction is on a whitelist of integer and SSE instructions
//    with exactly known operand roles (operand 0 is the only thing written),
//    and has no implicit operands.  That excludes every branch, call, return,
//    string op, push/pop (which would move ESP), MMX (aliases the x87
//    registers), FWAIT, and anything whose side effects are not listed here.
//    An x87 instruction in the gap is not on the list, so it ends the search.
//  * No gap instruction writes a register the FSTP address uses (base or
//    index), so the address computed at the FLD is the address the FSTP would
//    have used.
//  * Every memory operand of a gap instruction is provably disjoint from the
//    FSTP target: same base and index registers (hence same variable part)
//    and a non-overlapping displacement range, or two absolute addresses with
//    non-overlapping ranges.  Anything else (different base register, mixed
//    absolute/register, segment override, unknown size) may alias and rejects
//    the fusion.  With X87_FUSE_GAP_STRICT=1 the gap may not touch memory at
//    all.
//  * Block-level: if run bridging would join this FLD with later x87
//    instructions into one IR region, the bridge keeps priority.
//
// The FSTP's reply is only a no-op because the FLD's reply recorded the
// pairing in the cache (gap_tail_*); a FLD that stock translated itself
// (fallback, per-block exclusion) therefore never leaves a lone no-op FSTP.
//
// Values: bit-identical to the unfused replies.  The loaded value goes through
// the same FCVT s->d (a signalling NaN is quieted exactly as FLD m32 does) and
// the same FCVT d->s on the way out (same rounding, same NaN handling as
// FSTP m32); m64 -> m64 is a bit copy that quiets a signalling NaN (the unfused
// replies do, through the boundary conversion; so does hardware).  Status-word
// flags (C1, PE, IE, ...) are not touched, as in the unfused replies.  NZCV is
// not touched.
//
// Deliberate deviations from the unfused sidecar path, none of them visible to
// single-threaded code that does not fault:
//  1. The target store is performed before the gap instructions.  Memory is
//     disjoint, so a single thread cannot tell; another thread can only tell
//     if it relies on the order of two plain stores (or a load then a store)
//     within the same <=4-instruction window, which x86 guarantees.
//     X87_FUSE_GAP_STRICT=1 closes this by refusing gaps that touch memory.
//  2. If the target store faults, the fault is reported at the FLD (and none
//     of the gap instructions has run); after the handler fixes the mapping
//     and resumes, execution restarts at the FLD and the end state is the
//     same.  If a gap instruction faults or a signal lands in the gap, the
//     target store has already happened and the x87 stack lacks the temporary
//     that a handler would otherwise have seen in ST(0).
//  3. Stack overflow: with all eight x87 slots occupied, hardware raises a
//     stack fault and loads the indefinite QNaN.  Neither the unfused path
//     nor this one models that.  The unfused path overwrites the live slot
//     below TOP and then empties it; this path leaves the stack intact.  In
//     both the target receives the loaded value.
//  4. The physical register the temporary would have occupied keeps its old
//     bits instead of the copied value (hardware keeps the copy in the now
//     empty register).  Only FNSAVE/FXSAVE can see an empty register.
// =============================================================================

namespace TranslatorX87 {

namespace {

constexpr int kGapFuseMaxGap = 4;

enum class Role : uint8_t { kNone, kInt, kSse };

// Whitelist.  In every listed opcode operand 0 is the sole destination and
// the remaining operands are sources (verified against Rosetta's decoder on
// the instruction forms used by compilers; SSE operations appear in the
// normalized dst, src1[, src2] form).
Role gap_role(const IRInstr& ins) {
    switch (ins.opcode()) {
        case kOpcodeName_mov:
        case kOpcodeName_lea:
        case kOpcodeName_add:
        case kOpcodeName_sub:
        case kOpcodeName_and:
        case kOpcodeName_or:
        case kOpcodeName_xor:
        case kOpcodeName_cmp:
        case kOpcodeName_test:
        case kOpcodeName_inc:
        case kOpcodeName_dec:
        case kOpcodeName_neg:
        case kOpcodeName_not:
        case kOpcodeName_shl:
        case kOpcodeName_shr:
        case kOpcodeName_sar:
        case kOpcodeName_movzx:
        case kOpcodeName_movsx:
        case kOpcodeName_movsxd:
            return Role::kInt;
        case kOpcodeName_imul:
            // One-operand IMUL writes EDX:EAX implicitly.
            return ins.num_operands >= 2 ? Role::kInt : Role::kNone;
        case kOpcodeName_movss:
        case kOpcodeName_movsd:  // scalar (the string form is movsd_string)
        case kOpcodeName_movaps:
        case kOpcodeName_movups:
        case kOpcodeName_movapd:
        case kOpcodeName_movupd:
        case kOpcodeName_movdqa:
        case kOpcodeName_movdqu:
        case kOpcodeName_movd:
        case kOpcodeName_movq:
        case kOpcodeName_addss:
        case kOpcodeName_subss:
        case kOpcodeName_mulss:
        case kOpcodeName_divss:
        case kOpcodeName_minss:
        case kOpcodeName_maxss:
        case kOpcodeName_sqrtss:
        case kOpcodeName_addsd:
        case kOpcodeName_subsd:
        case kOpcodeName_mulsd:
        case kOpcodeName_divsd:
        case kOpcodeName_addps:
        case kOpcodeName_subps:
        case kOpcodeName_mulps:
        case kOpcodeName_divps:
        case kOpcodeName_andps:
        case kOpcodeName_andnps:
        case kOpcodeName_orps:
        case kOpcodeName_xorps:
        case kOpcodeName_pxor:
        case kOpcodeName_shufps:
        case kOpcodeName_unpcklps:
        case kOpcodeName_unpckhps:
        case kOpcodeName_pshufd:
        case kOpcodeName_cvtsi2ss:
        case kOpcodeName_cvtsi2sd:
        case kOpcodeName_cvtss2sd:
        case kOpcodeName_cvtsd2ss:
        case kOpcodeName_cvttss2si:
        case kOpcodeName_cvttsd2si:
        case kOpcodeName_comiss:
        case kOpcodeName_ucomiss:
        case kOpcodeName_comisd:
        case kOpcodeName_ucomisd:
            return Role::kSse;
        default:
            return Role::kNone;
    }
}

int size_bytes(IROperandSize s) {
    switch (s) {
        case IROperandSize::S8:
            return 1;
        case IROperandSize::S16:
            return 2;
        case IROperandSize::S32:
            return 4;
        case IROperandSize::S64:
            return 8;
        case IROperandSize::S128:
            return 16;
        case IROperandSize::S256:
            return 32;
        default:
            return 0;
    }
}

bool is_mem(const IROperand& op) {
    return op.kind == IROperandKind::MemRef || op.kind == IROperandKind::AbsMem;
}

// FLD source / FSTP target shape: 32- or 64-bit memory.
bool is_f32_f64_mem(const IROperand& op) {
    return is_mem(op) && (op.mem.size == IROperandSize::S32 || op.mem.size == IROperandSize::S64);
}

// Displacements that could wrap a 32-bit address are not worth reasoning about.
constexpr int64_t kMaxDisp = int64_t{1} << 30;

// True when the two operands provably address non-overlapping bytes.
bool provably_disjoint(const IROperand& a, int a_bytes, const IROperand& b, int b_bytes) {
    if (a_bytes <= 0 || b_bytes <= 0) {
        return false;
    }
    int64_t a_lo = 0;
    int64_t b_lo = 0;
    if (a.kind == IROperandKind::AbsMem && b.kind == IROperandKind::AbsMem) {
        if (a.abs_mem.addr_size != b.abs_mem.addr_size) {
            return false;
        }
        a_lo = a.abs_mem.value;
        b_lo = b.abs_mem.value;
    } else if (a.kind == IROperandKind::MemRef && b.kind == IROperandKind::MemRef) {
        const IROperandMemRef& x = a.mem;
        const IROperandMemRef& y = b.mem;
        if (x.seg_override != 0 || y.seg_override != 0 || x.addr_size != y.addr_size ||
            x.mem_flags != y.mem_flags) {
            return false;
        }
        const bool has_base = (x.mem_flags & 1U) != 0;
        const bool has_index = (x.mem_flags & 2U) != 0;
        if (has_base && x.base_reg != y.base_reg) {
            return false;
        }
        if (has_index && (x.index_reg != y.index_reg || x.shift_amount != y.shift_amount)) {
            return false;
        }
        if (std::llabs(x.disp) >= kMaxDisp || std::llabs(y.disp) >= kMaxDisp) {
            return false;
        }
        a_lo = x.disp;
        b_lo = y.disp;
    } else {
        return false;
    }
    return a_lo + a_bytes <= b_lo || b_lo + b_bytes <= a_lo;
}

// GPR "families" (index mod 16, with the high-byte AH/CH/DH/BH registers
// folded onto EAX/ECX/EDX/EBX) a MemRef address depends on.
uint32_t address_families(const IROperand& op) {
    if (op.kind != IROperandKind::MemRef) {
        return 0;
    }
    uint32_t m = 0;
    if ((op.mem.mem_flags & 1U) != 0) {
        m |= 1U << (op.mem.base_reg & 0xF);
    }
    if ((op.mem.mem_flags & 2U) != 0) {
        m |= 1U << (op.mem.index_reg & 0xF);
    }
    return m;
}

uint32_t register_family_bit(const IROperandRegister& r) {
    const bool high_byte = r.size == IROperandSize::S8 && (r.reg.value & 0xF0) == 0x10;
    const unsigned idx = high_byte ? (r.reg.value & 3U) : (r.reg.value & 0xFU);
    return 1U << idx;
}

// May `ins` sit between the FLD and the FSTP that stores to `target`?
bool gap_instruction_ok(const IRInstr& ins, const IROperand& target, int target_bytes,
                        bool strict) {
    const Role role = gap_role(ins);
    // SSE scalar forms carry their mandatory F3/F2 prefix in rep_prefix; a REP
    // prefix on an integer instruction would be a string-op idiom.
    if (role == Role::kNone || (role == Role::kInt && ins.rep_prefix != 0) ||
        ins.num_operands == 0 || ins.num_operands > 4) {
        return false;
    }
    const uint16_t opc = ins.opcode();
    const bool address_only = opc == kOpcodeName_lea;  // no memory access
    const uint32_t target_regs = address_families(target);

    for (int i = 0; i < ins.num_operands; ++i) {
        const IROperand& op = ins.operands[i];
        switch (op.kind) {
            case IROperandKind::Register:
                // GPRs and XMM only: ST(i) and MMX alias the x87 stack, YMM and
                // above are outside the whitelist's semantics.
                if (!op.reg.reg.is_gpr() && !op.reg.reg.is_xmm()) {
                    return false;
                }
                if (i == 0 && op.reg.reg.is_gpr() && opc != kOpcodeName_cmp &&
                    opc != kOpcodeName_test && (register_family_bit(op.reg) & target_regs) != 0) {
                    return false;  // the FSTP address would change under us
                }
                break;
            case IROperandKind::BranchOffset:  // plain immediate
                break;
            case IROperandKind::MemRef:
            case IROperandKind::AbsMem:
                if (address_only) {  // LEA only computes an address
                    if (op.kind == IROperandKind::MemRef && op.mem.seg_override != 0) {
                        return false;
                    }
                    break;
                }
                if (strict) {
                    return false;
                }
                if (op.kind == IROperandKind::MemRef && op.mem.seg_override != 0) {
                    return false;
                }
                if (!provably_disjoint(op, size_bytes(op.mem.size), target, target_bytes)) {
                    return false;
                }
                break;
            default:
                return false;  // fixup-carrying immediate, condition code, segment register
        }
    }
    return true;
}

}  // namespace

static auto match_gap_copy_impl(IRInstr* instrs, int64_t num, int64_t idx, int depth) -> int64_t;

// Does the request at idx start a fresh x87 run?  True when the previous
// instruction is not x87, or is the FSTP of an earlier fused copy (that reply
// armed no run, so the next request is fresh again: back-to-back copies chain).
static bool starts_fresh_run(IRInstr* instrs, int64_t num, int64_t idx, int depth) {
    if (idx == 0 || X87Cache::lookahead(instrs, num, idx - 1) == 0) {
        return true;
    }
    if (instrs[idx - 1].opcode() != kOpcodeName_fstp || depth >= 64) {
        return false;
    }
    // Find the FLD that heads the copy ending at idx - 1: the nearest x87
    // instruction before the gap.
    for (int64_t j = idx - 2; j >= 0 && j >= idx - 2 - kGapFuseMaxGap; --j) {
        if (X87Cache::lookahead(instrs, num, j) > 0) {
            return instrs[j].opcode() == kOpcodeName_fld &&
                   match_gap_copy_impl(instrs, num, j, depth + 1) == idx - 1;
        }
    }
    return false;
}

static auto match_gap_copy_impl(IRInstr* instrs, int64_t num, int64_t idx, int depth) -> int64_t {
    if (idx < 0 || idx + 2 >= num) {
        return -1;
    }
    const RosettaConfig* cfg = g_rosetta_config;
    if (cfg != nullptr && fusion_is_disabled(*cfg, FusionId::fld_gap_fstp)) {
        return -1;
    }
    const IRInstr& fld = instrs[idx];
    if (fld.opcode() != kOpcodeName_fld || fld.num_operands < 1 || fld.rep_prefix != 0 ||
        !is_f32_f64_mem(fld.operands[0])) {
        return -1;
    }
    // The FLD must start its own run: after an x87 instruction it would be
    // handled inside that run, with its cached state.
    if (!starts_fresh_run(instrs, num, idx, depth)) {
        return -1;
    }
    // Find the first x87 instruction after the gap; it must be our FSTP.
    int64_t tail = -1;
    for (int64_t j = idx + 1; j < num && j <= idx + 1 + kGapFuseMaxGap; ++j) {
        if (X87Cache::lookahead(instrs, num, j) > 0) {
            tail = j;
            break;
        }
    }
    if (tail < 0) {
        return -1;  // no x87 within the window
    }
    // Adjacent FLD;FSTP (no gap) is the same copy; take it only when the pair is
    // the whole run (anything longer belongs to the IR / the other fusions).
    if (tail == idx + 1 && tail + 1 < num && X87Cache::lookahead(instrs, num, tail + 1) > 0) {
        return -1;
    }
    const IRInstr& fstp = instrs[tail];
    if (fstp.opcode() != kOpcodeName_fstp || fstp.num_operands < 1 || fstp.rep_prefix != 0 ||
        !is_f32_f64_mem(fstp.operands[0])) {
        return -1;
    }
    const IROperand& target = fstp.operands[0];
    if (target.kind == IROperandKind::MemRef && target.mem.seg_override != 0) {
        return -1;
    }
    const int target_bytes = size_bytes(target.mem.size);
    const bool strict = cfg != nullptr && cfg->fuse_gap_strict != 0;
    for (int64_t j = idx + 1; j < tail; ++j) {
        if (!gap_instruction_ok(instrs[j], target, target_bytes, strict)) {
            return -1;
        }
    }
    // Bridging a region that contains this FLD keeps priority.
    if (cfg != nullptr && cfg->enable_bridge != 0) {
        const auto br =
            X87Cache::lookahead_bridged(instrs, num, idx, cfg->bridge_max_gap,
                                        cfg->bridge_max_total, cfg->enable_bridge_v2 != 0);
        if (br.total > 0) {
            return -1;
        }
    }
    return tail;
}

auto match_gap_copy(IRInstr* instrs, int64_t num, int64_t idx) -> int64_t {
    return match_gap_copy_impl(instrs, num, idx, 0);
}

auto classify_gap_fuse(TranslationResult& tr, IRBlock* block, IRInstr* instrs, int64_t num,
                       int64_t idx) -> GapFuse {
    if (idx < 0 || idx >= num) {
        return {};
    }
    const X87Cache& cache = tr.x87_cache;
    switch (instrs[idx].opcode()) {
        case kOpcodeName_fld: {
            const int64_t tail = match_gap_copy(instrs, num, idx);
            if (tail > idx) {
                return {GapFuseKind::kHead, tail};
            }
            return {};
        }
        case kOpcodeName_fstp:
            if (cache.gap_tail_block == block && cache.gap_tail_idx == idx &&
                match_gap_copy(instrs, num, cache.gap_head_idx) == idx) {
                return {GapFuseKind::kTail, idx};
            }
            return {};
        default:
            return {};
    }
}

void emit_gap_copy(TranslationResult& a1, IRInstr* fld, IRInstr* fstp) {
    AssemblerBuffer& buf = a1.insn_buf;
    const bool src_f32 = fld->operands[0].mem.size == IROperandSize::S32;
    const bool dst_f32 = fstp->operands[0].mem.size == IROperandSize::S32;

    // Both address computations are pure ALU work on guest registers; do them
    // ahead of the load so the load's latency overlaps the second one.
    const int Xsrc = compute_operand_address(a1, /*is_64bit=*/true, &fld->operands[0], GPR::XZR);
    const int Xdst = compute_operand_address(a1, /*is_64bit=*/true, &fstp->operands[0], GPR::XZR);

    if (!src_f32 && !dst_f32) {
        // m64 -> m64 must come out exactly as a push followed by a pop leaves
        // it: bit-identical, except that a signalling NaN is quieted (hardware
        // does so on the load, and so do the unfused replies, whose native-state
        // boundary quiets every NaN it converts).  Done in integer registers,
        // with no flag-setting instruction and no FP operation, so neither NZCV
        // nor FPCR.FZ can touch the bits:
        //     x |= ((0x7ff0000000000000 - (x & 0x7fff...)) >> 63) << 51
        // sets the quiet bit exactly when |x| is above infinity.
        const int Xv = alloc_free_gpr(a1);
        const int Xa = alloc_free_gpr(a1);
        const int Xi = alloc_free_gpr(a1);
        emit_ldr_imm(buf, /*size=*/3, Xv, Xsrc, /*imm12=*/0);
        free_gpr(a1, Xsrc);
        emit_bitfield(buf, /*is_64bit=*/1, /*opc=*/2 /*UBFM*/, /*N=*/1, /*immr=*/0, /*imms=*/62, Xv,
                      Xa);  // UBFX Xa, Xv, #0, #63
        emit_movn(buf, /*is_64bit=*/1, /*opc=*/2 /*MOVZ*/, /*hw=*/3, 0x7ff0, Xi);
        emit_add_sub_shifted_reg(buf, /*is_64bit=*/1, /*is_sub=*/1, /*is_set_flags=*/0,
                                 /*shift_type=*/0, /*Rm=*/Xa, /*shift_amount=*/0, /*Rn=*/Xi,
                                 /*Rd=*/Xa);  // Xa = inf - |x|
        emit_bitfield(buf, /*is_64bit=*/1, /*opc=*/2 /*UBFM*/, /*N=*/1, /*immr=*/63, /*imms=*/63,
                      Xa,
                      Xa);  // LSR Xa, Xa, #63
        emit_logical_shifted_reg(buf, /*is_64bit=*/1, /*opc=*/1 /*ORR*/, /*n=*/0, /*shift_type=*/0,
                                 /*Rm=*/Xa, /*shift_amount=*/51, /*Rn=*/Xv, /*Rd=*/Xv);
        emit_str_imm(buf, /*size=*/3, Xv, Xdst, /*imm12=*/0);
        free_gpr(a1, Xdst);
        free_gpr(a1, Xi);
        free_gpr(a1, Xa);
        free_gpr(a1, Xv);
        return;
    }

    const int Dd = alloc_free_fpr(a1);
    emit_fldr_imm(buf, src_f32 ? 2 : 3, Dd, Xsrc, /*imm12=*/0);
    free_gpr(a1, Xsrc);
    if (src_f32) {
        emit_fcvt_s_to_d(buf, Dd, Dd);  // quiets a signalling NaN, as FLD m32 does
    }
    if (dst_f32) {
        emit_fcvt_d_to_s(buf, Dd, Dd);  // rounds / narrows exactly as FSTP m32 does
    }
    emit_fstr_imm(buf, dst_f32 ? 2 : 3, Dd, Xdst, /*imm12=*/0);
    free_gpr(a1, Xdst);
    free_fpr(a1, Dd);
}

}  // namespace TranslatorX87
