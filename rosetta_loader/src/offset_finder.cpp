#include "offset_finder.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "macho_loader.hpp"
#include "types.h"

namespace {

using Image = std::vector<std::uint8_t>;

auto insnAt(const Image& image, std::uint64_t off) -> std::uint32_t {
    std::uint32_t v = 0;
    std::memcpy(&v, image.data() + off, sizeof(v));
    return v;
}

auto findAll(const Image& image, const std::uint8_t* pat, std::size_t len)
    -> std::vector<std::uint64_t> {
    std::vector<std::uint64_t> hits;
    const std::boyer_moore_searcher searcher(pat, pat + len);
    auto it = image.begin();
    while (true) {
        it = std::search(it, image.end(), searcher);
        if (it == image.end()) {
            break;
        }
        hits.push_back(static_cast<std::uint64_t>(std::distance(image.begin(), it)));
        ++it;
    }
    return hits;
}

// A section as [begin, end) file offsets; the whole image when absent.
struct Range {
    std::uint64_t begin;
    std::uint64_t end;
};

auto sectionRange(const MachoLoader& loader, const char* segment, const char* section) -> Range {
    if (auto* s = loader.getSection(segment, section)) {
        return {s->offset, s->offset + s->size};
    }
    return {0, loader.buffer_.size()};
}

// Offset of `name` as a whole NUL-delimited string inside `where`, or 0.
auto findCString(const Image& image, Range where, std::string_view name) -> std::uint64_t {
    std::string needle;
    needle.push_back('\0');
    needle.append(name);
    needle.push_back('\0');
    for (std::uint64_t hit :
         findAll(image, reinterpret_cast<const std::uint8_t*>(needle.data()), needle.size())) {
        if (hit + 1 >= where.begin && hit + 1 < where.end) {
            return hit + 1;
        }
    }
    return 0;
}

// ADRP: page-relative address of the instruction at `off`.
auto adrpTarget(std::uint32_t insn, std::uint64_t off) -> std::uint64_t {
    const std::uint64_t immlo = (insn >> 29) & 0x3;
    const std::uint64_t immhi = (insn >> 5) & 0x7FFFF;
    auto imm = static_cast<std::int64_t>((immhi << 2) | immlo) << 12;
    if (imm & (1LL << 32)) {
        imm |= ~((1LL << 33) - 1);
    }
    return (off & ~0xFFFULL) + static_cast<std::uint64_t>(imm);
}

auto isAdrp(std::uint32_t insn) -> bool {
    return (insn & 0x9F000000) == 0x90000000;
}
// add xd, xn, #imm12 (no shift)
auto isAddImm(std::uint32_t insn) -> bool {
    return (insn & 0xFFC00000) == 0x91000000;
}

// The `add xd, xn, #lo12` instructions that, together with a preceding
// `adrp xn`, materialise `target`. Returns the offsets of the adds.
auto findAdrpAddRefs(const Image& image, Range text, std::uint64_t target)
    -> std::vector<std::uint64_t> {
    std::vector<std::uint64_t> refs;
    for (std::uint64_t off = text.begin; off + 4 <= text.end; off += 4) {
        const std::uint32_t add = insnAt(image, off);
        if (!isAddImm(add)) {
            continue;
        }
        const std::uint32_t rn = (add >> 5) & 31;
        const std::uint64_t lo12 = (add >> 10) & 0xFFF;
        // The adrp is normally the previous instruction; allow a few in between.
        for (std::uint64_t back = 4; back <= 32 && off >= text.begin + back; back += 4) {
            const std::uint32_t adrp = insnAt(image, off - back);
            if (isAdrp(adrp) && (adrp & 31) == rn) {
                if (adrpTarget(adrp, off - back) + lo12 == target) {
                    refs.push_back(off);
                }
                break;
            }
        }
    }
    return refs;
}

// sub sp, sp, #imm12: the first instruction of every frame-setting prologue.
auto isSubSpImm(std::uint32_t insn) -> bool {
    return (insn & 0xFF8003FF) == 0xD10003FF;
}

// Start of the function containing `ref`: walk back to the nearest
// `sub sp, sp, #imm`. Function bodies only ever `add sp` (epilogues), so the
// first `sub sp` met walking backwards is the prologue, including for code
// laid out after the function's own `ret`.
auto findFunctionStart(const Image& image, Range text, std::uint64_t ref) -> std::uint64_t {
    for (std::uint64_t off = ref; off >= text.begin + 4 && ref - off < 0x8000; off -= 4) {
        if (isSubSpImm(insnAt(image, off))) {
            return off;
        }
    }
    return 0;
}

// bl target for the instruction at `off`, or 0.
auto blTarget(std::uint32_t insn, std::uint64_t off) -> std::uint64_t {
    if ((insn & 0xFC000000) != 0x94000000) {
        return 0;
    }
    auto imm = static_cast<std::int64_t>(insn & 0x03FFFFFF);
    if (imm & (1 << 25)) {
        imm |= ~((1LL << 26) - 1);
    }
    return off + static_cast<std::uint64_t>(imm * 4);
}

struct Export {
    std::uint64_t fn;
    std::string name;
};

// The {fn, name} table __DATA,exports points at (rosetta::runtime::library::*).
auto readExports(const MachoLoader& loader, const Exports* exports) -> std::vector<Export> {
    std::vector<Export> out;
    const Image& image = loader.buffer_;
    // Pointers carry dyld_chained_ptr_64_rebase metadata in their high bits.
    std::uint64_t table = exports->x87Exports & 0xFFFFFFFF;
    for (int i = 0; i < 512 && table + 16 <= image.size(); ++i, table += 16) {
        std::uint64_t fn = 0;
        std::uint64_t nm = 0;
        std::memcpy(&fn, image.data() + table, 8);
        std::memcpy(&nm, image.data() + table + 8, 8);
        fn &= 0xFFFFFFFF;
        nm &= 0xFFFFFFFF;
        if (fn == 0 || nm == 0 || nm >= image.size()) {
            break;
        }
        const char* s = reinterpret_cast<const char*>(image.data() + nm);
        out.push_back({fn, std::string(s, strnlen(s, image.size() - nm))});
    }
    return out;
}

}  // namespace

auto prologueIsRelocatable(const std::uint8_t* bytes, std::size_t len) -> bool {
    for (std::size_t i = 0; i + 4 <= len; i += 4) {
        std::uint32_t insn = 0;
        std::memcpy(&insn, bytes + i, 4);
        if ((insn & 0x1F000000) == 0x10000000 ||  // adr / adrp
            (insn & 0x7C000000) == 0x14000000 ||  // b / bl
            (insn & 0xFF000010) == 0x54000000 ||  // b.cond
            (insn & 0x7E000000) == 0x34000000 ||  // cbz / cbnz
            (insn & 0x7E000000) == 0x36000000 ||  // tbz / tbnz
            (insn & 0x3B000000) == 0x18000000) {  // ldr (literal)
            return false;
        }
    }
    return true;
}

// What the decode_opcode hook relies on, checked against the function body
// rather than assumed from a byte signature:
//   * it opens with `sub sp, sp, #imm` (the 16 displaced bytes are a plain
//     frame setup, and relocatable);
//   * within the first 16 instructions, `mov xN, x0` is followed by
//     `str xzr, [xN, #0x28]!`: the ctx pointer moves to xN, ctx->fault is at
//     +0x28 and xN becomes ctx+0x28;
//   * shortly after, `ldp xa, xb, [xN, #-0x20]` loads code_base/code_end from
//     ctx+0x08/+0x10 and `stp xc, xc, [xN, #-0x10]` seeds insn_start/cursor
//     at ctx+0x18/+0x20, which is exactly the DecoderCtx layout the hook
//     swaps and restores (stub_asm.hpp, kDecoderCtx*);
//   * `str x3, [sp]` stashes out_len in the frame within the first 32
//     instructions, the calling convention the hook's second call assumes.
auto decodeOpcodeShapeOk(const std::vector<std::uint8_t>& image, std::uint64_t off) -> bool {
    if (off == 0 || off + 32 * 4 > image.size()) {
        return false;
    }
    if (!isSubSpImm(insnAt(image, off)) || !prologueIsRelocatable(image.data() + off, 16)) {
        return false;
    }
    std::uint64_t faultStore = 0;
    std::uint32_t ctxReg = 0;
    for (std::uint64_t i = 1; i < 16; ++i) {
        const std::uint32_t mov = insnAt(image, off + i * 4);
        if ((mov & 0xFFFFFFE0) != 0xAA0003E0) {  // mov xN, x0
            continue;
        }
        const std::uint32_t n = mov & 31;
        if (insnAt(image, off + (i + 1) * 4) == (0xF8028C1F | (n << 5))) {  // str xzr,[xN,#0x28]!
            faultStore = off + (i + 1) * 4;
            ctxReg = n;
            break;
        }
    }
    if (faultStore == 0) {
        return false;
    }
    bool sawLoadBounds = false;
    bool sawSeedCursor = false;
    for (std::uint64_t i = 1; i <= 8; ++i) {
        const std::uint32_t insn = insnAt(image, faultStore + i * 4);
        if ((insn & 0xFFFF83E0) == (0xA97E0000 | (ctxReg << 5))) {  // ldp xa, xb, [xN, #-0x20]
            sawLoadBounds = true;
        }
        if ((insn & 0xFFFF83E0) == (0xA93F0000 | (ctxReg << 5)) &&
            (insn & 31) == ((insn >> 10) & 31)) {  // stp xc, xc, [xN, #-0x10]
            sawSeedCursor = true;
        }
    }
    bool sawOutLenStash = false;
    for (std::uint64_t i = 1; i < 32; ++i) {
        if (insnAt(image, off + i * 4) == 0xF90003E3) {  // str x3, [sp]
            sawOutLenStash = true;
        }
    }
    return sawLoadBounds && sawSeedCursor && sawOutLenStash;
}

auto OffsetFinder::determineOffsets() -> bool {
    // byte patterns in hex for the functions we need to find.
    const std::vector<unsigned char> exportsFetch = {0x62, 0x06, 0x40, 0xF9,
                                                     0x63, 0x12, 0x40, 0xB9};
    const std::vector<unsigned char> svcCall = {0xB0, 0x18, 0x80, 0xD2, 0x01, 0x10, 0x00, 0xD4,
                                                0xE1, 0x37, 0x9F, 0x9A, 0xC0, 0x03, 0x5F, 0xD6};
    const std::vector<unsigned char> disableAot = {
        0xFD, 0x43, 0x07, 0x91,
        0xE4, 0x2B, 0x00, 0xF9};  // ADRP + STRB pattern for g_disable_aot global variable
    // For svc_call we need to check where this bitpattern starts in the code and also where it ends
    // (we can just add 0xC to the start to get the end)

    MachoLoader runtime;
    if (!runtime.open("/usr/libexec/rosetta/runtime")) {
        fprintf(stdout, "Cannot open /usr/libexec/rosetta/runtime to determine offsets\n");
        return false;
    }
    const Image& buffer = runtime.buffer_;

    // Do the search and store the results
    std::vector<std::uint64_t> results;
    for (const auto& offset : {exportsFetch, svcCall, disableAot}) {
        const auto hits = findAll(buffer, offset.data(), offset.size());
        if (hits.empty()) {
            fprintf(stdout, "Pattern %zu not found in /usr/libexec/rosetta/runtime\n",
                    results.size());
            results.push_back(-1);
        } else {
            if (hits.size() > 1) {
                fprintf(stdout,
                        "Pattern %zu matches %zu places in /usr/libexec/rosetta/runtime; "
                        "using the first\n",
                        results.size(), hits.size());
            }
            results.push_back(hits[0]);
        }
    }

    // If we've stored -1 in any offset, error out and fall back to non-accelerated x87 handles.
    if (static_cast<int>(results[0]) <= -1 || static_cast<int>(results[1]) <= -1 ||
        static_cast<int>(results[2]) <= -1) {
        return false;
    }

    // Set the offsets to the results that we've found now that we know they're "correct".
    offsetExportsFetch_ = results[0];
    offsetSvcCallEntry_ = results[1];
    offsetSvcCallRet_ = offsetSvcCallEntry_ + 0xC;

    // extract the g_disable_aot offset from the disableAot pattern
    /*
    00 D0 29 00 80 52
__text:00000000000147A8 FD 43 07 91                 ADD             X29, SP, #0x1D0
__text:00000000000147AC E4 2B 00 F9                 STR             X4, [SP,#0x1D0+var_180]
__text:00000000000147B0 28 01 00 F0                 ADRP            X8, #disable_aot@PAGE
__text:00000000000147B4 08 F1 4F 39                 LDRB            W8, [X8,#disable_aot@PAGEOFF]
    */
    uint32_t adrp_offset = results[2] + 8;

    uint32_t adrp_instruction = insnAt(buffer, adrp_offset);
    uint32_t ldrb_instruction = insnAt(buffer, adrp_offset + 4);
    if (!isAdrp(adrp_instruction) || (ldrb_instruction & 0xFFC00000) != 0x39400000) {
        fprintf(stdout,
                "g_disable_aot: unexpected instructions after the anchor in "
                "/usr/libexec/rosetta/runtime\n");
        return false;
    }
    // Decode LDRB (unsigned offset): pageoff = imm12 (bits [21:10]), no shift for byte access
    uint64_t ldrb_imm12 = (ldrb_instruction >> 10) & 0xFFF;
    offsetDisableAot_ = adrpTarget(adrp_instruction, adrp_offset) + ldrb_imm12;

    // The sampler's fragment lookup: a plain tree search rooted at a global,
    // which panics with a fixed string when the pc is in no fragment. Find the
    // string's reference, then the last `adrp xM; add xM, xM, #lo` before it
    // that does not build the string itself: that is the root. Best effort.
    const Range text = sectionRange(runtime, "__TEXT", "__text");
    allocatorCallerCodeChecks_ = discoverAllocatorCaller(buffer, text.begin, text.end);
    if (allocatorCallerCodeChecks_.empty()) {
        std::fprintf(stdout, "runtime allocation caller lock contract is unsupported\n");
        return false;
    }
    const Range cstrings = sectionRange(runtime, "__TEXT", "__cstring");
    if (const std::uint64_t panic =
            findCString(buffer, cstrings, "cannot locate code fragment for arm address")) {
        for (const std::uint64_t ref : findAdrpAddRefs(buffer, text, panic)) {
            for (std::uint64_t back = 4; back <= 64 * 4 && ref >= text.begin + back; back += 4) {
                const std::uint32_t add = insnAt(buffer, ref - back);
                if (!isAddImm(add) || (add & 31) != ((add >> 5) & 31) || (add & 31) == 0) {
                    continue;
                }
                const std::uint32_t adrp = insnAt(buffer, ref - back - 4);
                if (!isAdrp(adrp) || (adrp & 31) != (add & 31)) {
                    continue;
                }
                const std::uint64_t target =
                    adrpTarget(adrp, ref - back - 4) + ((add >> 10) & 0xFFF);
                // The root is a global in __DATA or __bss, past __text and
                // possibly past the end of the file (bss has no file bytes).
                if (target >= text.end && target < text.end + 0x400000) {
                    armTreeRootOffset_ = target;
                }
                break;
            }
            if (armTreeRootOffset_ != 0) {
                break;
            }
        }
    }

    return true;
}

auto OffsetFinder::determineRuntimeOffsets() -> bool {
    const std::vector<unsigned char> translation_result_size_pattern = {0x01, 0x4D, 0x80, 0x52};

    MachoLoader libRosettaRuntimeLoader;
    if (!libRosettaRuntimeLoader.open("/Library/Apple/usr/libexec/oah/libRosettaRuntime")) {
        fprintf(stdout,
                "Failed to open libRosettaRuntime Mach-O file to determine runtime offsets "
                "automatically.\n");
        return false;
    }
    const Image& image = libRosettaRuntimeLoader.buffer_;

    auto* text_section = libRosettaRuntimeLoader.getSection("__TEXT", "__text");
    if (!text_section) {
        fprintf(stdout,
                "Failed to find __TEXT.__text section in libRosettaRuntime Mach-O file to "
                "determine runtime offsets automatically.\n");
        return false;
    }
    const Range text = sectionRange(libRosettaRuntimeLoader, "__TEXT", "__text");
    const Range cstrings = sectionRange(libRosettaRuntimeLoader, "__TEXT", "__cstring");

    auto* exports_section = libRosettaRuntimeLoader.getSection("__DATA", "exports");
    if (!exports_section) {
        fprintf(stdout,
                "Failed to find __DATA.exports section in libRosettaRuntime Mach-O file to "
                "determine runtime offsets automatically.\n");
        return false;
    }
    auto* exports = reinterpret_cast<const Exports*>(image.data() + exports_section->offset);

    const auto x87_exports_rva = exports->x87Exports & 0xFFFFFFFF;
    offsetInitLibrary_ =
        (*reinterpret_cast<const uint64_t*>(image.data() + x87_exports_rva)) & 0xFFFFFFFF;

    // Exports.version is a build constant baked into the on-disk __DATA,exports
    // (not a rebased pointer), so it reads correctly from the file. Used to seed
    // the runtime version in both attach modes.
    runtimeVersion_ = exports->version;

    // translator_translate is exported by name. Its body calls translate_insn
    // once per instruction and hands its allocator the TranslationResult size
    // as an immediate, so it anchors both.
    const auto exportsTable = readExports(libRosettaRuntimeLoader, exports);
    std::uint64_t ttEnd = 0;
    for (const auto& e : exportsTable) {
        if (e.name.find("translator_translateE") != std::string::npos) {
            offsetTranslatorTranslate_ = e.fn;
        }
    }
    if (offsetTranslatorTranslate_ != 0) {
        ttEnd = std::min<std::uint64_t>(offsetTranslatorTranslate_ + 0x4000, text.end);
        for (const auto& e : exportsTable) {
            if (e.fn > offsetTranslatorTranslate_ && e.fn < ttEnd) {
                ttEnd = e.fn;
            }
        }
        for (std::uint64_t off = offsetTranslatorTranslate_; off + 4 <= ttEnd; off += 4) {
            const std::uint32_t insn = insnAt(image, off);
            if (translationResultSize_ == 0 && (insn & 0xFFE0001F) == 0x52800001) {  // movz w1,#imm
                translationResultSize_ = (insn >> 5) & 0xFFFF;
            }
        }
    }

    // translate_insn: the callee of translator_translate that names itself
    // "translate_instruction" in its asserts. The prologue signature is the
    // fallback and the cross-check.
    const auto prologueHits =
        findAll(image, kTranslateInsnPattern.data(), kTranslateInsnPattern.size());
    std::uint64_t byCaller = 0;
    if (offsetTranslatorTranslate_ != 0) {
        std::vector<std::uint64_t> selfRefs;
        if (const std::uint64_t name = findCString(image, cstrings, "translate_instruction")) {
            selfRefs = findAdrpAddRefs(image, text, name);
        }
        std::vector<std::uint64_t> candidates;
        for (std::uint64_t off = offsetTranslatorTranslate_; off + 4 <= ttEnd; off += 4) {
            const std::uint64_t target = blTarget(insnAt(image, off), off);
            if (target == 0 || target < text.begin || target + 16 > text.end ||
                !isSubSpImm(insnAt(image, target))) {
                continue;
            }
            const bool namesItself =
                std::any_of(selfRefs.begin(), selfRefs.end(), [&](std::uint64_t ref) {
                    return ref > target && ref - target < 0x8000 &&
                           findFunctionStart(image, text, ref) == target;
                });
            if (namesItself &&
                std::find(candidates.begin(), candidates.end(), target) == candidates.end()) {
                candidates.push_back(target);
            }
        }
        if (candidates.size() == 1) {
            byCaller = candidates[0];
        }
    }
    if (byCaller != 0) {
        offsetTranslateInsn_ = byCaller;
        if (prologueHits.empty() || prologueHits[0] != byCaller) {
            fprintf(stdout,
                    "translate_insn: located at 0x%llx via translator_translate; the prologue "
                    "signature %s\n",
                    static_cast<unsigned long long>(byCaller),
                    prologueHits.empty() ? "no longer matches it" : "matches elsewhere first");
        }
    } else if (!prologueHits.empty()) {
        offsetTranslateInsn_ = prologueHits[0];
    } else {
        fprintf(stdout, "translate_insn not found in libRosettaRuntime\n");
        return false;
    }
    std::memcpy(translateInsnPrologue_.data(), image.data() + offsetTranslateInsn_, 16);
    if (!prologueIsRelocatable(translateInsnPrologue_.data(), 16)) {
        fprintf(stdout, "translate_insn: its first 16 bytes are not relocatable; cannot hook\n");
        return false;
    }

    // The TranslationResult size immediate, kept for the verbose log.
    if (const auto hits = findAll(image, translation_result_size_pattern.data(),
                                  translation_result_size_pattern.size());
        !hits.empty()) {
        offsetTransactionResultSize_ = hits[0];
    }

    // decode_opcode: the function that names itself "decode_opcode" in its
    // asserts, accepted only if it still has the shape the hook relies on.
    // Falls back to the prologue signatures. Best effort: the hook is optional.
    if (const std::uint64_t name = findCString(image, cstrings, "decode_opcode")) {
        for (const std::uint64_t ref : findAdrpAddRefs(image, text, name)) {
            const std::uint64_t start = findFunctionStart(image, text, ref);
            if (decodeOpcodeShapeOk(image, start)) {
                offsetDecodeOpcode_ = start;
                break;
            }
        }
    }
    if (offsetDecodeOpcode_ == 0) {
        for (const auto& [pat, len] :
             {std::pair{kDecodeOpcodePattern.data(), kDecodeOpcodePattern.size()},
              std::pair{kDecodeOpcodePatternV2.data(), kDecodeOpcodePatternV2.size()}}) {
            const auto hits = findAll(image, pat, len);
            if (hits.size() == 1 && decodeOpcodeShapeOk(image, hits[0])) {
                offsetDecodeOpcode_ = hits[0];
                break;
            }
        }
    }
    if (offsetDecodeOpcode_ != 0) {
        std::memcpy(decodeOpcodePrologue_.data(), image.data() + offsetDecodeOpcode_, 16);
    }

    if (const auto* data = libRosettaRuntimeLoader.getSection("__DATA", "__bss");
        data != nullptr && data->size <= UINT64_MAX - data->addr) {
        auto allocator = discoverRuntimeAllocator(image, text.begin, text.end, data->addr,
                                                  data->addr + data->size);
        offsetAssemblerBufferGrow_ = allocator.grow;
        offsetArenaAllocate_ = allocator.arenaAllocate;
        offsetArenaPointer_ = allocator.arenaPointer;
        allocatorCodeChecks_ = std::move(allocator.codeChecks);
    }

    // The opcode mnemonic table, in host enum order: what the runtime's own
    // module printer uses. Read it whole; the loader checks the entries the
    // stub filter's opcode ranges depend on against it.
    static const char kTableHead[] = "\0aaa\0aad\0aam\0aas\0adc\0add\0";
    const auto heads =
        findAll(image, reinterpret_cast<const std::uint8_t*>(kTableHead), sizeof(kTableHead) - 1);
    if (heads.size() == 1) {
        std::uint64_t p = heads[0] + 1;
        const std::uint64_t end = std::min<std::uint64_t>(cstrings.end, image.size());
        while (p < end && opcodeNames_.size() < 1024) {
            const char* s = reinterpret_cast<const char*>(image.data() + p);
            const std::size_t n = strnlen(s, end - p);
            if (n == 0) {
                break;
            }
            opcodeNames_.emplace_back(s, n);
            p += n + 1;
        }
    }

    return true;
}

namespace {

// The audited bodies fix the buffer ABI, arena ownership, and fatal allocation
// failure contract. Only address-bearing immediates may vary across builds.
constexpr std::array<std::uint32_t, 20> kAllocatorEmit = {
    0xa9be4ff4, 0xa9017bfd, 0x910043fd, 0xaa0103f4, 0xaa0003f3, 0xa940a408, 0x9100110a,
    0xeb09015f, 0x54000083, 0xaa1303e0, 0x940005fb, 0xf9400668, 0xf9400269, 0xb8286934,
    0xf9400668, 0x91001108, 0xf9000668, 0xa9417bfd, 0xa8c24ff4, 0xd65f03c0,
};
constexpr std::array<std::uint32_t, 37> kAllocatorGrow = {
    0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd, 0xaa0003f3, 0x52880008, 0xf9400809, 0xd37ff92a,
    0xf100013f, 0x9a8a0114, 0xb9401808, 0x340000c8, 0xf0000328, 0xf946fd00, 0xaa1403e1, 0x94012575,
    0x14000004, 0xaa1403e0, 0x52801cc1, 0x94012599, 0xaa0003f5, 0xf9400261, 0xb4000121, 0xf9400a62,
    0xaa1503e0, 0x97fff616, 0xb9401a68, 0x35000088, 0xf9400260, 0xf9400a61, 0x97fff7c0, 0xf9000275,
    0xf9000a74, 0xa9427bfd, 0xa9414ff4, 0xa8c357f6, 0xd65f03c0,
};
constexpr std::array<std::uint32_t, 40> kAllocatorArena = {
    0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd, 0x91003c28, 0xf27ced08, 0x54000380, 0xaa0003f3,
    0xf9400009, 0xb4000169, 0xf9400d2a, 0xab08014b, 0x54000362, 0xf940092c, 0xeb0c017f, 0x540000a2,
    0x8b0a0120, 0x8b080148, 0xf9000d28, 0x14000010, 0x91008115, 0x52a00408, 0xf14802bf, 0x9a8882b4,
    0xaa1403e0, 0x52801d81, 0x9400000e, 0xaa0003e8, 0x91008000, 0xf9400269, 0xa900d109, 0xf9000268,
    0xf9000d15, 0x14000002, 0xd2800000, 0xa9427bfd, 0xa9414ff4, 0xa8c357f6, 0xd65f03c0, 0xd4200020,
};
constexpr std::array<std::uint32_t, 22> kAllocatorMap = {
    0xd100c3ff, 0xa9014ff4, 0xa9027bfd, 0x910083fd, 0xaa0003f3, 0x53081c24, 0xd2c00020, 0xaa1303e1,
    0x52800062, 0x52820043, 0xd2800005, 0x97fed25d, 0xb100041f, 0x540000a0, 0xa9427bfd, 0xa9414ff4,
    0x9100c3ff, 0xd65f03c0, 0xf90003f3, 0xf00000a0, 0x91072400, 0x94000971,
};
constexpr std::array<std::uint32_t, 17> kAllocatorFatal = {
    0xa9be4ff4, 0xa9017bfd, 0x910043fd, 0xd11043ff, 0xaa0003e2, 0x910043a8,
    0xf90007e8, 0x910043f3, 0x910043e0, 0x910043a3, 0x52808001, 0x97fffd78,
    0xf90003f3, 0xb00000a0, 0x91068800, 0x97ffffca, 0xd4200020,
};
constexpr std::array<std::uint32_t, 5> kAllocatorCopy = {
    0xa9bf7bfd, 0x910003fd, 0xcb010003, 0xeb02007f, 0x54000a43,
};
constexpr std::array<std::uint32_t, 5> kAllocatorUnmapCall = {
    0xd2800930, 0xd4001001, 0x92800001, 0x9a802020, 0xd65f03c0,
};
constexpr std::array<std::uint32_t, 5> kAllocatorMapCall = {
    0xd28018b0, 0xd4001001, 0x92800001, 0x9a802020, 0xd65f03c0,
};

struct MaskedWord {
    size_t index;
    std::uint32_t mask;
};

// Every read is within the executable section and the file. PC-relative calls
// are decoded and checked separately before any discovered address is returned.
template <size_t N>
bool allocationShape(const Image& image, Range text, std::uint64_t offset,
                     const std::array<std::uint32_t, N>& words,
                     std::initializer_list<MaskedWord> variable = {}) {
    if (offset < text.begin || offset > text.end || N * 4 > text.end - offset) {
        return false;
    }
    for (size_t i = 0; i < N; ++i) {
        std::uint32_t mask = UINT32_MAX;
        for (const auto& change : variable) {
            if (change.index == i) {
                mask = change.mask;
            }
        }
        if ((insnAt(image, offset + i * 4) & mask) != (words[i] & mask)) {
            return false;
        }
    }
    return true;
}

}  // namespace

auto discoverRuntimeAllocator(const std::vector<std::uint8_t>& image, std::uint64_t textBegin,
                              std::uint64_t textEnd, std::uint64_t dataBegin, std::uint64_t dataEnd)
    -> RuntimeAllocatorDiscovery {
    if (textBegin % 4 != 0 || textEnd % 4 != 0 || textBegin >= textEnd || textEnd > image.size() ||
        dataBegin >= dataEnd) {
        return {};
    }
    const Range text{textBegin, textEnd};
    constexpr std::uint32_t kCall = 0xfc000000;
    constexpr std::uint32_t kPage = 0x9f00001f;
    constexpr std::uint32_t kOffset = 0xffc003ff;
    RuntimeAllocatorDiscovery result;
    for (std::uint64_t emit = textBegin; textEnd - emit >= kAllocatorEmit.size() * 4; emit += 4) {
        if (!allocationShape(image, text, emit, kAllocatorEmit, {{10, kCall}})) {
            continue;
        }
        const auto call = [&](std::uint64_t at) { return blTarget(insnAt(image, at), at); };
        const std::uint64_t grow = call(emit + 0x28);
        if (!allocationShape(
                image, text, grow, kAllocatorGrow,
                {{12, kPage}, {13, kOffset}, {15, kCall}, {19, kCall}, {25, kCall}, {30, kCall}})) {
            continue;
        }
        const std::uint64_t arena = call(grow + 0x3c);
        const std::uint64_t map = call(grow + 0x4c);
        const std::uint64_t copy = call(grow + 0x64);
        const std::uint64_t unmap = call(grow + 0x78);
        if (!allocationShape(image, text, arena, kAllocatorArena, {{26, kCall}}) ||
            !allocationShape(image, text, map, kAllocatorMap,
                             {{11, kCall}, {19, kPage}, {20, kOffset}, {21, kCall}}) ||
            !allocationShape(image, text, copy, kAllocatorCopy) ||
            !allocationShape(image, text, unmap, kAllocatorUnmapCall) ||
            call(arena + 0x68) != map) {
            continue;
        }
        const std::uint64_t mapCall = call(map + 0x2c);
        const std::uint64_t fatal = call(map + 0x54);
        if (!allocationShape(image, text, mapCall, kAllocatorMapCall) ||
            !allocationShape(image, text, fatal, kAllocatorFatal,
                             {{11, kCall}, {13, kPage}, {14, kOffset}, {15, kCall}})) {
            continue;
        }
        // The fatal path ends in BRK, with no C++ exception-unwind path. Its two
        // diagnostic calls must still name code within this runtime image.
        const auto validCall = [&](std::uint64_t at) {
            const auto target = call(at);
            return target >= textBegin && target < textEnd;
        };
        if (!validCall(fatal + 0x2c) || !validCall(fatal + 0x3c)) {
            continue;
        }
        const auto load = insnAt(image, grow + 0x34);
        const auto page = adrpTarget(insnAt(image, grow + 0x30), grow + 0x30);
        const auto low = static_cast<std::uint64_t>((load >> 10) & 0xfff) * 8;
        if (page > UINT64_MAX - low) {
            continue;
        }
        const auto pointer = page + low;
        if (pointer % 8 != 0 || pointer < dataBegin || pointer > dataEnd ||
            dataEnd - pointer < sizeof(std::uint64_t)) {
            continue;
        }
        if (result.grow != 0) {
            return {};  // A second complete candidate makes the ABI ambiguous.
        }
        result.grow = grow;
        result.arenaAllocate = arena;
        result.arenaPointer = pointer;
        const auto snapshot = [&](std::uint64_t offset, size_t count) {
            result.codeChecks.push_back(
                {offset, {image.begin() + offset, image.begin() + offset + count * 4}});
        };
        snapshot(emit, kAllocatorEmit.size());
        snapshot(grow, kAllocatorGrow.size());
        snapshot(arena, kAllocatorArena.size());
        snapshot(map, kAllocatorMap.size());
        snapshot(fatal, kAllocatorFatal.size());
        snapshot(copy, kAllocatorCopy.size());
        snapshot(unmap, kAllocatorUnmapCall.size());
        snapshot(mapCall, kAllocatorMapCall.size());
    }
    return result;
}

auto discoverAllocatorCaller(const std::vector<std::uint8_t>& image, std::uint64_t textBegin,
                             std::uint64_t textEnd) -> std::vector<RuntimeCodeCheck> {
    if (textBegin % 4 != 0 || textEnd % 4 != 0 || textBegin >= textEnd || textEnd > image.size()) {
        return {};
    }
    // These whole-body fingerprints pin the audited external-lock contract.
    // A changed runtime needs a new control-flow audit, not a relaxed match.
    constexpr std::array<std::uint8_t, CC_SHA256_DIGEST_LENGTH> kCallerDigest = {
        0xb5, 0x5f, 0x41, 0x55, 0xdb, 0x75, 0x13, 0xff, 0x03, 0x8b, 0x39,
        0xc2, 0xa4, 0x4d, 0xff, 0x1b, 0x2f, 0x66, 0xa8, 0x08, 0x05, 0xe4,
        0x74, 0xdb, 0x42, 0xa7, 0x0b, 0x60, 0xa0, 0x84, 0x7d, 0x38,
    };
    constexpr std::array<std::uint8_t, CC_SHA256_DIGEST_LENGTH> kLocksDigest = {
        0x39, 0x43, 0x15, 0x36, 0xc2, 0x9b, 0xc4, 0x2a, 0x27, 0xd9, 0x8d,
        0xd2, 0xd0, 0xcd, 0x57, 0xc3, 0x1c, 0x53, 0xaa, 0x1e, 0x9e, 0x75,
        0x49, 0x68, 0xd6, 0x6e, 0x0f, 0x4b, 0x71, 0x0a, 0xc5, 0x85,
    };
    constexpr std::array<std::uint8_t, CC_SHA256_DIGEST_LENGTH> kThunkDigest = {
        0x4b, 0xbb, 0x49, 0x53, 0x27, 0xf2, 0xf4, 0x2e, 0xdd, 0x73, 0x40,
        0xef, 0xda, 0xf0, 0x91, 0x1f, 0xb9, 0x65, 0xc9, 0x5f, 0x95, 0xa0,
        0x3e, 0x1d, 0x89, 0xd1, 0xbf, 0xce, 0xd6, 0x17, 0x76, 0xba,
    };
    constexpr std::array<std::uint32_t, 9> kCallerStart = {0xa9ba6ffc, 0xa90167fa, 0xa9025ff8,
                                                           0xa90357f6, 0xa9044ff4, 0xa9057bfd,
                                                           0x910143fd, 0xd10883ff, 0xf90013e4};
    constexpr size_t kCallerBytes = 0xfdc;
    constexpr size_t kLockBytes = 0x140;
    constexpr size_t kThunkBytes = 16;
    const Range text{textBegin, textEnd};
    const auto fingerprint = [&](std::uint64_t offset, size_t length, const auto& expected) {
        if (offset < textBegin || offset > textEnd || length > textEnd - offset) {
            return false;
        }
        std::array<std::uint8_t, CC_SHA256_DIGEST_LENGTH> digest{};
        CC_SHA256(image.data() + offset, static_cast<CC_LONG>(length), digest.data());
        return digest == expected;
    };
    for (std::uint64_t caller = textBegin; textEnd - caller >= kCallerBytes; caller += 4) {
        if (!allocationShape(image, text, caller, kCallerStart) ||
            !fingerprint(caller, kCallerBytes, kCallerDigest)) {
            continue;
        }
        const auto call = [&](std::uint64_t at) { return blTarget(insnAt(image, at), at); };
        const auto lock = call(caller + 0x60);
        const auto thunk = call(caller + 0x40c);
        if (!fingerprint(lock, kLockBytes, kLocksDigest) ||
            !fingerprint(thunk, kThunkBytes, kThunkDigest) || call(caller + 0x6c) != lock ||
            call(caller + 0xfa8) != lock + 0xc4 || call(caller + 0xfb4) != lock + 0xc4) {
            return {};
        }
        // Bind the verified thunk to the named import, rather than infer its
        // identity from its location in the runtime's function-pointer table.
        const auto page = adrpTarget(insnAt(image, thunk), thunk);
        const auto low = ((insnAt(image, thunk + 4) >> 10) & 0xfff) +
                         ((insnAt(image, thunk + 8) >> 10) & 0xfff) * 8;
        if (page > UINT64_MAX - low) {
            return {};
        }
        const auto slot = page + low;
        if (slot > image.size() || image.size() - slot < 16) {
            return {};
        }
        std::uint64_t name = 0;
        std::memcpy(&name, image.data() + slot + 8, sizeof(name));
        name &= 0xffffffff;  // dyld_chained_ptr_64_rebase metadata lives above RVA32.
        constexpr char kTranslateName[] =
            "__ZN7rosetta7runtime7library20translator_translateEPKNS1_"
            "12ModuleResultE15TranslationMode";
        if (name > image.size() || sizeof(kTranslateName) > image.size() - name ||
            std::memcmp(image.data() + name, kTranslateName, sizeof(kTranslateName)) != 0) {
            return {};
        }
        size_t callers = 0;
        for (std::uint64_t at = textBegin; at < textEnd; at += 4) {
            callers += blTarget(insnAt(image, at), at) == thunk;
        }
        if (callers != 1) {
            return {};  // Every stock call must share the audited lock context.
        }
        return {{caller, {image.begin() + caller, image.begin() + caller + kCallerBytes}},
                {lock, {image.begin() + lock, image.begin() + lock + kLockBytes}},
                {thunk, {image.begin() + thunk, image.begin() + thunk + kThunkBytes}}};
    }
    return {};
}
