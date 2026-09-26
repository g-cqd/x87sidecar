// Exercise the request boundary and its private cache without a public test API.
// Parent-memory operations use the real Mach task port and page protections.
#include "../rosetta_loader/src/sidecar.cpp"

#include <sys/mman.h>

#include "rosetta_core/Register.h"

namespace {
int failures = 0;

void check(bool passed, const char* name) {
    std::printf("%s  %s\n", passed ? "PASS" : "FAIL", name);
    failures += !passed;
}

struct Fixture {
    size_t page_size = static_cast<size_t>(getpagesize());
    void* tr_page = mmap(nullptr, page_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    void* code_page = mmap(nullptr, page_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TranslationResult* tr = nullptr;
    ThreadContextOffsets tco{};
    IRInstr ir{};
    sidecar::TranslateRequest request{};

    Fixture() {
        assert(tr_page != MAP_FAILED && code_page != MAP_FAILED);
        tr = new (tr_page) TranslationResult{};
        tr->insn_buf = {static_cast<uint32_t*>(code_page), 0, page_size, 0};
        tr->thread_context_offsets = &tco;
        tr->free_gpr_mask = kGprScratchMask;
        tr->free_fpr_mask = kFprScratchMask;
        tr->_unoccupied_temporary_fprs_for_xmm_scalars = kFprScratchMask;
        ir.set_opcode(kOpcodeName_fld1);
        request = {reinterpret_cast<uint64_t>(tr), 0x1234,
                   reinterpret_cast<uint64_t>(&ir), 1, 0};
        sidecar::g_x87Cache.clear();
        sidecar::g_irCache.clear();
        sidecar::g_tcoCache.clear();
        auto& cache = sidecar::g_x87Cache[request.tr_addr];
        cache.prev_block = reinterpret_cast<IRBlock*>(0x5678);
        cache.gprs_valid = 1;
        cache.top_dirty = 1;
        cache.run_remaining = 2;
    }

    ~Fixture() {
        assert(mprotect(tr_page, page_size, PROT_READ | PROT_WRITE) == 0);
        tr->~TranslationResult();
        munmap(tr_page, page_size);
        munmap(code_page, page_size);
    }

    bool invalidated() const {
        const auto& cache = sidecar::g_x87Cache.at(request.tr_addr);
        return cache.prev_block == nullptr && !cache.gprs_valid && !cache.top_dirty &&
               !cache.active();
    }

    sidecar::TranslateOutcome translate() {
        return sidecar::processTranslateRequest(mach_task_self(), request);
    }
};

void failed_output_write() {
    Fixture fixture;
    assert(mprotect(fixture.code_page, fixture.page_size, PROT_READ) == 0);
    const auto result = fixture.translate();
    check(!result.reply_some && fixture.tr->insn_buf.end == 0 && fixture.invalidated(),
          "failed emitted-code write invalidates the prior cache before stock fallback");
}

void failed_result_write() {
    Fixture fixture;
    assert(mprotect(fixture.tr_page, fixture.page_size, PROT_READ) == 0);
    const auto result = fixture.translate();
    check(!result.reply_some && fixture.tr->insn_buf.end == 0 && fixture.invalidated(),
          "failed final result write invalidates the translated cache before stock fallback");
}

void rejected_request() {
    Fixture fixture;
    fixture.request.num_instrs = 0;
    check(!fixture.translate().reply_some && fixture.invalidated(),
          "a malformed continuation invalidates the prior cache before stock fallback");
}

void failed_result_read() {
    Fixture fixture;
    assert(mprotect(fixture.tr_page, fixture.page_size, PROT_NONE) == 0);
    check(!fixture.translate().reply_some && fixture.invalidated(),
          "an unreadable result invalidates the prior cache before stock fallback");
}

void successful_request() {
    Fixture fixture;
    const auto result = fixture.translate();
    check(result.reply_some && result.value == 1 && fixture.tr->insn_buf.end > 0 &&
              sidecar::g_x87Cache.at(fixture.request.tr_addr).prev_block ==
                  reinterpret_cast<IRBlock*>(fixture.request.block),
          "successful write-back retains the translated cache");
}
}  // namespace

int main() {
    successful_request();
    failed_output_write();
    failed_result_write();
    rejected_request();
    failed_result_read();
    return failures != 0;
}
