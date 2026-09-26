// Execute the production ARM stub with a local replacement for its Mach trap.
// The replacement supplies real protocol bytes; allocator calls use test-owned
// native storage so the test can inspect every published range.
#include <libkern/OSCacheControl.h>
#include <mach/message.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>

#include "../rosetta_loader/src/stub_asm.cpp"

namespace {
using namespace stub_asm;
struct Scenario {
    TranslationResult tr{};
    IRInstr ir{};
    sidecar::TranslationReply first{};
    std::array<uint64_t, 5> request{};
    std::vector<void*> allocations;
    unsigned requests = 0;
    unsigned grows = 0;
    unsigned arena_calls = 0;
    bool repeat_reserve = false;
    bool reject_allocations = false;
    uint32_t reply_size = sizeof(mach_msg_header_t) + sizeof(sidecar::TranslationReply);

    ~Scenario() {
        tr.external_fixups.begin = nullptr;
        tr._fixups.begin = nullptr;
        for (void* p : allocations) {
            std::free(p);
        }
    }
}* scenario;

void* allocate(size_t size) {
    void* p = std::malloc(size);
    assert(p != nullptr);
    scenario->allocations.push_back(p);
    return p;
}

extern "C" void native_grow(AssemblerBuffer* buffer) {
    if (scenario->reject_allocations) {
        _exit(42);
    }
    ++scenario->grows;
    const uint64_t capacity = buffer->end_cap == 0 ? 0x4000 : buffer->end_cap * 2;
    auto* replacement = static_cast<uint32_t*>(allocate(capacity));
    if (buffer->end != 0) {
        std::memcpy(replacement, buffer->data, buffer->end);
    }
    buffer->data = replacement;
    buffer->end_cap = capacity;
}

extern "C" void* native_arena(void* arena, uint64_t bytes) {
    if (scenario->reject_allocations) {
        _exit(42);
    }
    assert(arena == scenario);
    ++scenario->arena_calls;
    return allocate(bytes);
}

extern "C" uint64_t fake_mach_call(void* bytes, uint64_t, uint64_t, uint64_t, uint64_t id, uint64_t,
                                   uint64_t, uint64_t) {
    auto* header = static_cast<mach_msg_header_t*>(bytes);
    assert(std::memcmp(header + 1, scenario->request.data(), sizeof(scenario->request)) == 0);
    const bool first = scenario->requests++ == 0;
    header->msgh_size = scenario->reply_size;
    header->msgh_id = static_cast<int32_t>(id >> 32);
    const sidecar::TranslationReply reply =
        first || scenario->repeat_reserve
            ? scenario->first
            : sidecar::TranslationReply{.result = 1, .kind = sidecar::ReplyKind::Some};
    std::memcpy(header + 1, &reply, sizeof(reply));
    return 0;
}

uint64_t execute(Scenario& fixture) {
    scenario = &fixture;
    fixture.ir.set_opcode(kOpcodeName_fld1);
    fixture.request = {reinterpret_cast<uint64_t>(&fixture.tr), 0x1234,
                       reinterpret_cast<uint64_t>(&fixture.ir), 1, 0};
    constexpr size_t mapping_size = 16384;
    void* mapping =
        mmap(nullptr, mapping_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(mapping != MAP_FAILED);
    const auto address = reinterpret_cast<uint64_t>(mapping);
    const uint32_t prologue[] = {RET_INSN, RET_INSN, RET_INSN, RET_INSN};
    auto blobs =
        build(address, address, reinterpret_cast<const uint8_t*>(prologue), 1, 2,
              {reinterpret_cast<uint64_t>(native_grow), reinterpret_cast<uint64_t>(native_arena),
               reinterpret_cast<uint64_t>(&scenario)});
    assert(!blobs.entry.empty());
    auto& code = blobs.handler;
    const size_t trampoline = code.size();
    for (size_t i = 4; i < trampoline; i += 4) {
        uint32_t previous, instruction;
        std::memcpy(&previous, code.data() + i - 4, 4);
        std::memcpy(&instruction, code.data() + i, 4);
        if (previous == movn(16, 46, 0) && instruction == svc(0x80)) {
            patch_word(code, i, bl(static_cast<int32_t>((trampoline - i) / 4)));
        }
    }
    emit(code, stp_preindex(29, 30, stub_asm::SP, -16));
    emit_load_imm64(code, 16, reinterpret_cast<uint64_t>(fake_mach_call));
    emit(code, 0xD63F0200U);  // blr x16
    emit(code, ldp_postindex(29, 30, stub_asm::SP, 16));
    emit(code, RET_INSN);
    assert(code.size() <= mapping_size);
    std::memcpy(mapping, code.data(), code.size());
    assert(mprotect(mapping, mapping_size, PROT_READ | PROT_EXEC) == 0);
    sys_icache_invalidate(mapping, code.size());
    using Entry = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    const auto& args = fixture.request;
    const auto result =
        reinterpret_cast<Entry>(mapping)(args[0], args[1], args[2], args[3], args[4]);
    assert(munmap(mapping, mapping_size) == 0);
    return result;
}

void reserve_all_buffers() {
    Scenario fixture;
    const std::array<uint32_t, 4> original_code{0x12345678, 2, 3, 4};
    fixture.tr.insn_buf = {const_cast<uint32_t*>(original_code.data()), 16, 16, 1};
    std::array<Fixup, 2> original_fixups{};
    std::memset(original_fixups.data(), 0xAB, sizeof(original_fixups));
    for (auto* list : {&fixture.tr.external_fixups, &fixture.tr._fixups}) {
        list->begin = original_fixups.data();
        list->end = list->end_cap = original_fixups.data() + original_fixups.size();
        list->_size = 1;
    }
    fixture.first = {
        .result = 68, .kind = sidecar::ReplyKind::Reserve, .fixup_capacities = {48, 60}};
    assert(execute(fixture) == 1);
    assert(fixture.requests == 2 && fixture.grows == 3 && fixture.arena_calls == 2);
    assert(fixture.tr.insn_buf.end == 16 && fixture.tr.insn_buf.end_cap == 128);
    assert(std::memcmp(fixture.tr.insn_buf.data, original_code.data(), 16) == 0);
    for (auto* list : {&fixture.tr.external_fixups, &fixture.tr._fixups}) {
        assert(list->_size == 1 && list->end - list->begin == 2);
        assert(std::memcmp(list->begin, original_fixups.data(), sizeof(original_fixups)) == 0);
    }
    assert(fixture.tr.external_fixups.end_cap - fixture.tr.external_fixups.begin == 4);
    assert(fixture.tr._fixups.end_cap - fixture.tr._fixups.begin == 5);
    std::puts("PASS  reserve grows all native buffers once and retries the original request");
}

void fitting_replies() {
    Scenario handled;
    handled.first = {.result = 1, .kind = sidecar::ReplyKind::Some};
    assert(execute(handled) == 1);
    assert(handled.requests == 1 && handled.grows == 0 && handled.arena_calls == 0);
    Scenario stock;
    assert(execute(stock) == reinterpret_cast<uint64_t>(&stock.tr));
    assert(stock.requests == 1 && stock.grows == 0 && stock.arena_calls == 0);
    std::puts("PASS  fitting replies use one IPC and preserve the Some/None dispatch");
}

void rejects_bad_reply(unsigned kind) {
    std::fflush(nullptr);
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        Scenario fixture;
        fixture.reject_allocations = kind != 5;
        fixture.first = {.result = 36, .kind = sidecar::ReplyKind::Reserve};
        if (kind >= 10 && kind <= 15) {
            auto& list = fixture.tr.external_fixups;
            list.begin = reinterpret_cast<Fixup*>(0x1000);
            list.end = list.end_cap = reinterpret_cast<Fixup*>(0x100C);
            fixture.first.fixup_capacities[0] = 48;
        }
        if (kind >= 16) {
            fixture.tr.insn_buf = {reinterpret_cast<uint32_t*>(0x1000), 0, 4, 1};
        }
        switch (kind) {
            case 0:
                fixture.first.kind = static_cast<sidecar::ReplyKind>(4);
                break;
            case 1:
                fixture.first.result = sidecar::kMaxReserveBytes + 4;
                break;
            case 2:
                fixture.first.result = 3;
                break;
            case 3:
                fixture.first.fixup_capacities[0] = 4;
                break;
            case 4:
                fixture.first.result = 0;
                break;
            case 5:
                fixture.repeat_reserve = true;
                break;
            case 6:
                fixture.first = {.result = 0, .kind = sidecar::ReplyKind::Some};
                break;
            case 7:
                fixture.first = {.result = 2, .kind = sidecar::ReplyKind::Some};
                break;
            case 8:
                fixture.reply_size = 40;
                break;
            case 9:
                fixture.tr.external_fixups.begin = reinterpret_cast<Fixup*>(0x1000);
                fixture.tr.external_fixups.end = reinterpret_cast<Fixup*>(0x1018);
                fixture.tr.external_fixups.end_cap = reinterpret_cast<Fixup*>(0x1018);
                fixture.first.fixup_capacities[0] = 12;
                break;
            case 10:
                fixture.tr.external_fixups.end = reinterpret_cast<Fixup*>(0x1001);
                break;
            case 11:
                fixture.tr.external_fixups.end_cap = reinterpret_cast<Fixup*>(0x1000);
                break;
            case 12:
                fixture.tr.external_fixups.begin = reinterpret_cast<Fixup*>(0x1002);
                fixture.tr.external_fixups.end = reinterpret_cast<Fixup*>(0x100E);
                fixture.tr.external_fixups.end_cap = reinterpret_cast<Fixup*>(0x101A);
                break;
            case 13:
                fixture.tr.external_fixups.begin = fixture.tr.external_fixups.end = nullptr;
                fixture.tr.external_fixups.end_cap = reinterpret_cast<Fixup*>(12);
                break;
            case 14:
                fixture.tr.external_fixups._size = 2;
                break;
            case 15:
                fixture.tr.external_fixups.end_cap = reinterpret_cast<Fixup*>(0x1010);
                break;
            case 16:
                fixture.tr.insn_buf.data = nullptr;
                break;
            case 17:
                fixture.tr.insn_buf.end = 1;
                break;
            case 18:
                fixture.tr.insn_buf.end_cap = 2;
                break;
            case 19:
                fixture.tr.insn_buf.data = reinterpret_cast<uint32_t*>(UINT64_MAX - 3);
                break;
            case 20:
                fixture.tr.insn_buf.use_heap = 2;
                break;
            case 21:
                fixture.tr.insn_buf.end = 8;
                break;
            case 22:
                fixture.tr.insn_buf.end_cap = 12;
                fixture.first.result = sidecar::kMaxReserveBytes;
                break;
            case 23:
                fixture.first = {.result = kOpcodeName_arpl, .kind = sidecar::ReplyKind::Fatal};
                break;
        }
        execute(fixture);
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 137);
}
}  // namespace

int main() {
    reserve_all_buffers();
    fitting_replies();
    for (unsigned kind = 0; kind < 24; ++kind) {
        rejects_bad_reply(kind);
    }
    std::puts("PASS  invalid kinds, capacities and repeated reserves fail before translation");
}
