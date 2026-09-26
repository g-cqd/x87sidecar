// Exercise the request boundary and its private cache without a public test API.
// Parent-memory operations use the real Mach task port and page protections.
#include <mach/mach_vm.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <utility>

namespace {
unsigned remote_allocations = 0;
unsigned remote_writes = 0;

kern_return_t observe_allocate(vm_map_t task, mach_vm_address_t* address, mach_vm_size_t size,
                               int flags) {
    ++remote_allocations;
    return mach_vm_allocate(task, address, size, flags);
}

kern_return_t observe_write(vm_map_t task, mach_vm_address_t address, vm_offset_t data,
                            mach_msg_type_number_t size) {
    ++remote_writes;
    return mach_vm_write(task, address, data, size);
}
}  // namespace

#define mach_vm_allocate observe_allocate
#define mach_vm_write observe_write
#include "../rosetta_loader/src/sidecar.cpp"
#undef mach_vm_write
#undef mach_vm_allocate
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
        tr->field_B0.begin = nullptr;
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
    check(!result.reply_some && result.stock_fallback_safe && fixture.tr->insn_buf.end == 0 &&
              fixture.invalidated(),
          "failed emitted-code write invalidates the prior cache before stock fallback");
}

void failed_result_write() {
    Fixture fixture;
    assert(mprotect(fixture.tr_page, fixture.page_size, PROT_READ) == 0);
    const auto result = fixture.translate();
    check(!result.reply_some && result.stock_fallback_safe && fixture.tr->insn_buf.end == 0 &&
              fixture.invalidated(),
          "failed final result write invalidates the translated cache before stock fallback");
}

void rejected_request() {
    Fixture fixture;
    fixture.request.num_instrs = 0;
    const auto result = fixture.translate();
    check(!result.reply_some && !result.stock_fallback_safe && fixture.invalidated(),
          "a malformed continuation invalidates the prior cache and refuses unsafe fallback");
}

void unknown_opcode() {
    Fixture fixture;
    fixture.ir.opcode_ = UINT16_MAX;
    remote_writes = 0;
    const auto result = fixture.translate();
    check(!result.reply_some && !result.stock_fallback_safe && fixture.invalidated() &&
              remote_writes == 0,
          "an unknown opcode refuses unsafe fallback before publication");
}

void failed_result_read() {
    Fixture fixture;
    assert(mprotect(fixture.tr_page, fixture.page_size, PROT_NONE) == 0);
    check(!fixture.translate().reply_some && fixture.invalidated(),
          "an unreadable result invalidates the prior cache before refusing the request");
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

void empty_parent_capacity(uint32_t ownership) {
    Fixture fixture;
    fixture.tr->insn_buf = {nullptr, 0, 0, ownership};
    std::array<uint8_t, sidecar::kStockTRSize> before{};
    std::memcpy(before.data(), fixture.tr, before.size());
    remote_allocations = remote_writes = 0;
    const auto result = fixture.translate();
    check(!result.reply_some && result.reserve[0] > 0 && remote_allocations == 0 &&
              remote_writes == 0 && std::memcmp(before.data(), fixture.tr, before.size()) == 0,
          ownership == 0 ? "empty mapped capacity requests native reserve without publication"
                         : "empty heap capacity requests native reserve without publication");
    if (fixture.tr->insn_buf.data != nullptr) {
        mach_vm_deallocate(mach_task_self(), reinterpret_cast<uint64_t>(fixture.tr->insn_buf.data),
                           fixture.tr->insn_buf.end_cap);
    }
}

void fixup_capacity(bool sufficient) {
    Fixture fixture;
    fixture.ir.set_opcode(kOpcodeName_fld);
    fixture.ir.num_operands = 1;
    fixture.ir.operands[0].imm = {.kind = IROperandKind::Immediate,
                                  .size = IROperandSize::S64,
                                  .addr_size = IROperandSize::S64,
                                  .mem_flags = 1,
                                  .value = 0x1234};
    std::array<Fixup, 2> storage{};
    auto& list = fixture.tr->external_fixups;
    list.begin = list.end = storage.data();
    list.end_cap = storage.data() + (sufficient ? 2 : 1);
    std::array<uint8_t, sidecar::kStockTRSize> before{};
    std::memcpy(before.data(), fixture.tr, before.size());
    remote_allocations = remote_writes = 0;
    const auto result = fixture.translate();
    if (sufficient) {
        check(result.reply_some && remote_allocations == 0 && list.begin == storage.data() &&
                  list.end == storage.data() + 2 && storage[0].kind == FixupKind::Arm64Page21 &&
                  storage[1].kind == FixupKind::Arm64PageOffset12,
              "exact fixup capacity preserves its allocation and appends both relocations");
    } else {
        check(!result.reply_some && result.reserve[1] >= 2 * sizeof(Fixup) &&
                  remote_allocations == 0 && remote_writes == 0 &&
                  std::memcmp(before.data(), fixture.tr, before.size()) == 0,
              "insufficient fixup capacity requests native reserve before writing code");
    }
    if (list.begin != storage.data()) {
        mach_vm_deallocate(
            mach_task_self(), reinterpret_cast<uint64_t>(list.begin),
            reinterpret_cast<uint64_t>(list.end_cap) - reinterpret_cast<uint64_t>(list.begin));
    }
}

void opaque_stock_metadata(size_t live_bytes, size_t capacity, uint64_t count) {
    Fixture fixture;
    alignas(16) std::array<uint8_t, 64> storage{};
    auto& list = fixture.tr->field_B0;
    list.begin = reinterpret_cast<Fixup*>(storage.data());
    list.end = reinterpret_cast<Fixup*>(storage.data() + live_bytes);
    list.end_cap = reinterpret_cast<Fixup*>(storage.data() + capacity);
    list._size = count;
    std::array<uint8_t, sizeof(list)> before{};
    std::memcpy(before.data(), &list, before.size());
    const auto result = fixture.translate();
    check(result.reply_some && std::memcmp(before.data(), &list, before.size()) == 0,
          "opaque stock metadata preserves its 16-byte records while translation accelerates");
}

void fractional_fixup_capacity(bool near_address_limit) {
    Fixture fixture;
    auto& list = fixture.tr->external_fixups;
    const auto start =
        near_address_limit ? UINT64_MAX - 7 : reinterpret_cast<uint64_t>(fixture.code_page);
    list.begin = list.end = reinterpret_cast<Fixup*>(start);
    list.end_cap = reinterpret_cast<Fixup*>(start + 4);
    const auto reads_before = sidecar::g_statVmSyscalls.load();
    remote_allocations = remote_writes = 0;
    const auto result = fixture.translate();
    check(!result.reply_some && fixture.invalidated() && remote_allocations == 0 &&
              remote_writes == 0 && sidecar::g_statVmSyscalls.load() == reads_before + 1,
          near_address_limit ? "partial fixup capacity near UINT64_MAX is rejected before use"
                             : "partial fixup capacity is rejected before use");
}

void reserve_retry_preserves_translation() {
    Fixture fixture;
    std::array<IRInstr, 6> instructions{};
    instructions.fill(fixture.ir);
    fixture.request.instr_array = reinterpret_cast<uint64_t>(instructions.data());
    fixture.request.num_instrs = instructions.size();
    const auto initial_gprs = fixture.tr->free_gpr_mask;
    const auto initial_fprs = fixture.tr->free_fpr_mask;
    const auto initial_cache = sidecar::g_x87Cache.at(fixture.request.tr_addr);
    const auto full = fixture.translate();
    assert(full.reply_some);
    std::vector<uint8_t> expected(fixture.tr->insn_buf.end);
    std::memcpy(expected.data(), fixture.code_page, expected.size());
    const auto expected_cache = sidecar::g_x87Cache.at(fixture.request.tr_addr);

    fixture.tr->insn_buf.end = 0;
    fixture.tr->insn_buf.end_cap = 0;
    fixture.tr->free_gpr_mask = initial_gprs;
    fixture.tr->free_fpr_mask = initial_fprs;
    sidecar::g_x87Cache[fixture.request.tr_addr] = initial_cache;
    const auto reserve = fixture.translate();
    const auto saved_cache = sidecar::g_x87Cache.at(fixture.request.tr_addr);
    check(!reserve.reply_some && reserve.reserve[0] == expected.size() &&
              fixture.tr->insn_buf.end == 0 &&
              std::memcmp(&saved_cache, &initial_cache, sizeof(saved_cache)) == 0,
          "reserve preserves the previously committed cache and every instruction boundary");
    fixture.tr->insn_buf.end_cap = fixture.page_size;
    const auto retry = fixture.translate();
    const auto& actual_cache = sidecar::g_x87Cache.at(fixture.request.tr_addr);
    check(retry.reply_some && retry.value == full.value &&
              fixture.tr->insn_buf.end == expected.size() &&
              std::memcmp(expected.data(), fixture.code_page, expected.size()) == 0 &&
              actual_cache.profile_hash == expected_cache.profile_hash &&
              actual_cache.last_next_idx == expected_cache.last_next_idx && !actual_cache.active(),
          "reserve retry emits identical complete native-state boundaries and full-block hash");
}

void synthetic_failure(unsigned kind) {
    Fixture fixture;
    fixture.ir.set_opcode(kOpcodeName_arpl);
    fixture.ir.num_operands = 2;
    fixture.ir.operands[0].reg = {
        .kind = IROperandKind::Register, .size = IROperandSize::S16, .reg = {0}};
    fixture.ir.operands[1].reg = {
        .kind = IROperandKind::Register, .size = IROperandSize::S16, .reg = {2}};
    assert(fixture.translate().reply_some);
    const auto before = fixture.tr->insn_buf;
    if (kind == 0) {
        fail_next_allocation = true;
    } else if (kind == 1) {
        assert(mprotect(fixture.code_page, fixture.page_size, PROT_READ) == 0);
    } else {
        assert(mprotect(fixture.tr_page, fixture.page_size, PROT_READ) == 0);
    }
    const auto result = fixture.translate();
    check(!result.reply_some && !result.stock_fallback_safe &&
              result.reserve == std::array<uint64_t, 3>{} && fixture.invalidated() &&
              std::memcmp(&before, &fixture.tr->insn_buf, sizeof(before)) == 0,
          kind == 0   ? "synthetic ARPL allocation failure cannot fall through to stock"
          : kind == 1 ? "synthetic ARPL code-write failure cannot fall through to stock"
                      : "synthetic ARPL result-write failure cannot fall through to stock");
    fail_next_allocation = false;
}

void diagnostic_stock_mode(bool synthetic) {
    Fixture fixture;
    if (synthetic) {
        fixture.ir.set_opcode(kOpcodeName_arpl);
    }
    RosettaConfig config{};
    config.loader_always_none = 1;
    rosetta_set_config(&config);
    const auto result = fixture.translate();
    rosetta_set_config(nullptr);
    check(!result.reply_some && result.instruction_known &&
              result.stock_fallback_safe == !synthetic && fixture.invalidated(),
          synthetic ? "diagnostic stock mode refuses synthetic ARPL fallback"
                    : "diagnostic stock mode still delegates real opcodes");
}

void early_allocation_failure(bool synthetic) {
    Fixture fixture;
    if (synthetic) {
        fixture.ir.set_opcode(kOpcodeName_arpl);
    }
    fail_next_allocation = true;  // first IR-cache allocation, before opcode identification
    const auto result = fixture.translate();
    check(!result.reply_some && result.instruction_known &&
              result.stock_fallback_safe == !synthetic && fixture.invalidated(),
          synthetic ? "early allocation failure identifies ARPL without allocation"
                    : "early allocation failure preserves stock fallback for a real opcode");
    fail_next_allocation = false;
}
}  // namespace

int main() {
    successful_request();
    failed_output_write();
    failed_result_write();
    rejected_request();
    unknown_opcode();
    failed_result_read();
    huge_spare_capacity();
    failed_initial_allocation();
    malformed_fixup_list();
    scratch_growth_keeps_parent_storage();
    scratch_capacity_stays_within_parent_capacity();
    empty_parent_capacity(0);
    empty_parent_capacity(1);
    fixup_capacity(true);
    fixup_capacity(false);
    opaque_stock_metadata(16, 16, 1);
    opaque_stock_metadata(48, 64, 3);
    fractional_fixup_capacity(false);
    fractional_fixup_capacity(true);
    reserve_retry_preserves_translation();
    for (unsigned kind = 0; kind < 3; ++kind) {
        synthetic_failure(kind);
    }
    diagnostic_stock_mode(false);
    diagnostic_stock_mode(true);
    early_allocation_failure(false);
    early_allocation_failure(true);
    for (unsigned kind = 0; kind < 5; ++kind) {
        isolated_malformed_buffer(kind);
    }
    return failures != 0;
}
