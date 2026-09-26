// Exercise the request boundary and its private cache without a public test API.
// Parent-memory operations use the real Mach task port and page protections.
#include <sys/mman.h>
#include <sys/wait.h>

#include <utility>

#include "../rosetta_loader/src/sidecar.cpp"
#include "rosetta_core/Register.h"

namespace {
bool fail_next_allocation = false;
std::size_t largest_allocation = 0;
}  // namespace

// Fault injection stays in this executable; the production allocator is unchanged.
void* operator new(std::size_t size) {
    largest_allocation = std::max(largest_allocation, size);
    if (std::exchange(fail_next_allocation, false)) {
        throw std::bad_alloc();
    }
    if (void* allocation = std::malloc(std::max<std::size_t>(size, 1))) {
        return allocation;
    }
    throw std::bad_alloc();
}

void operator delete(void* allocation) noexcept {
    std::free(allocation);
}

namespace {
int failures = 0;

void check(bool passed, const char* name) {
    std::printf("%s  %s\n", passed ? "PASS" : "FAIL", name);
    failures += !passed;
}

struct Fixture {
    size_t page_size = static_cast<size_t>(getpagesize());
    size_t code_size = page_size * 32;
    void* tr_page =
        mmap(nullptr, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    void* code_page =
        mmap(nullptr, code_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
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
        request = {reinterpret_cast<uint64_t>(tr), 0x1234, reinterpret_cast<uint64_t>(&ir), 1, 0};
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
        tr->external_fixups.begin = nullptr;
        tr->~TranslationResult();
        munmap(tr_page, page_size);
        munmap(code_page, code_size);
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

void huge_spare_capacity() {
    Fixture fixture;
    fixture.tr->insn_buf.end_cap = UINT64_C(1) << 61;
    bool translated = false;
    try {
        translated = fixture.translate().reply_some;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "unexpected spare-capacity allocation failure: %s\n", error.what());
    }
    check(translated && fixture.tr->insn_buf.end > 0,
          "huge remote spare capacity does not determine local allocation");
}

bool malformed_buffer(unsigned kind) {
    Fixture fixture;
    switch (kind) {
        case 0:
            fixture.tr->insn_buf.end = 4;
            fixture.tr->insn_buf.end_cap = 0;
            break;
        case 1:
            fixture.tr->insn_buf.end = 1;
            break;
        case 2:
            fixture.tr->insn_buf.data = nullptr;
            break;
        case 3:
            fixture.tr->insn_buf.end_cap = UINT64_MAX - 3;
            break;
        case 4:
            fixture.tr->insn_buf.end = UINT64_C(1) << 61;
            fixture.tr->insn_buf.end_cap = fixture.tr->insn_buf.end;
            break;
    }
    const auto before = fixture.tr->insn_buf;
    const auto reads_before = sidecar::g_statVmSyscalls.load();
    const auto result = fixture.translate();
    return !result.reply_some && fixture.invalidated() &&
           std::memcmp(&before, &fixture.tr->insn_buf, sizeof(before)) == 0 &&
           sidecar::g_statVmSyscalls.load() == reads_before + 1;
}

void isolated_malformed_buffer(unsigned kind) {
    // A missing boundary check can otherwise abort the whole regression binary.
    std::fflush(nullptr);
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        _exit(!malformed_buffer(kind));
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    constexpr const char* names[] = {
        "a live prefix past capacity declines before dependent memory access",
        "a misaligned live prefix declines before dependent memory access",
        "a null allocated buffer declines before dependent memory access",
        "an overflowing capacity range declines before dependent memory access",
        "an excessive live prefix declines before dependent memory access",
    };
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0, names[kind]);
}

void failed_initial_allocation() {
    Fixture fixture;
    assert(fixture.translate().reply_some);
    const auto before = fixture.tr->insn_buf;
    fail_next_allocation = true;
    bool declined = false;
    try {
        declined = !fixture.translate().reply_some;
    } catch (const std::bad_alloc&) {
        std::fprintf(stderr, "request allocation escaped the boundary\n");
    }
    check(declined && !fail_next_allocation && fixture.invalidated() &&
              std::memcmp(&before, &fixture.tr->insn_buf, sizeof(before)) == 0,
          "failed local allocation returns stock fallback without publishing partial state");
    fail_next_allocation = false;
}

void malformed_fixup_list() {
    Fixture fixture;
    auto& fixups = fixture.tr->external_fixups;
    fixups.begin = static_cast<Fixup*>(fixture.code_page);
    fixups.end = reinterpret_cast<Fixup*>(reinterpret_cast<uint64_t>(fixups.begin) - 4);
    fixups.end_cap = fixups.begin;
    const auto reads_before = sidecar::g_statVmSyscalls.load();
    check(!fixture.translate().reply_some && fixture.invalidated() &&
              sidecar::g_statVmSyscalls.load() == reads_before + 1,
          "reversed fixup bounds decline before any dependent read or write");
}

void scratch_growth_keeps_parent_storage() {
    Fixture fixture;
    std::vector<IRInstr> instructions(1024, fixture.ir);
    fixture.request.instr_array = reinterpret_cast<uint64_t>(instructions.data());
    fixture.request.num_instrs = instructions.size();
    fixture.tr->insn_buf.end_cap = fixture.code_size;
    RosettaConfig config{};
    config.disable_x87_ir = 1;
    rosetta_set_config(&config);
    const auto result = fixture.translate();
    rosetta_set_config(nullptr);
    check(result.reply_some && result.value == static_cast<int64_t>(instructions.size()) &&
              fixture.tr->insn_buf.end > 0x4000 && fixture.tr->insn_buf.data == fixture.code_page,
          "scratch growth appends in place when the parent still has capacity");
}

void scratch_capacity_stays_within_parent_capacity() {
    Fixture fixture;
    fixture.tr->insn_buf.end = 4096;
    largest_allocation = 0;
    const auto result = fixture.translate();
    check(result.reply_some && largest_allocation <= fixture.page_size,
          "initial scratch allocation does not exceed the existing parent capacity");
}
}  // namespace

int main() {
    successful_request();
    failed_output_write();
    failed_result_write();
    rejected_request();
    failed_result_read();
    huge_spare_capacity();
    failed_initial_allocation();
    malformed_fixup_list();
    scratch_growth_keeps_parent_storage();
    scratch_capacity_stays_within_parent_capacity();
    for (unsigned kind = 0; kind < 5; ++kind) {
        isolated_malformed_buffer(kind);
    }
    return failures != 0;
}
