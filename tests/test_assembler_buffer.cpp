#include <sys/mman.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "rosetta_core/AssemblerBuffer.h"

namespace {
int failures = 0;

void check(bool passed, const char* name) {
    std::printf("%s  %s\n", passed ? "PASS" : "FAIL", name);
    failures += !passed;
}

void exact_capacity() {
    std::array<uint32_t, 2> storage{0, 0xfeedface};
    AssemblerBuffer buffer{storage.data(), 0, sizeof(uint32_t), AssemblerBuffer::kBorrowedHeap};
    buffer.emit(0x12345678);
    check(buffer.data == storage.data() && buffer.end_cap == sizeof(uint32_t) &&
              buffer.end == sizeof(uint32_t) && storage[0] == 0x12345678 &&
              storage[1] == 0xfeedface,
          "the last available instruction fits without allocating");
    if (buffer.data != storage.data()) {
        std::free(buffer.data);
    }
}

void borrowed_growth() {
    std::array<uint32_t, 2> storage{0x12345678, 0xfeedface};
    AssemblerBuffer buffer{storage.data(), sizeof(uint32_t), sizeof(uint32_t),
                           AssemblerBuffer::kBorrowedHeap};
    for (uint32_t i = 1; i < 1024; ++i) {
        buffer.emit(i);
    }
    bool preserved = buffer.data[0] == 0x12345678;
    for (uint32_t i = 1; i < 1024; ++i) {
        preserved &= buffer.data[i] == i;
    }
    check(preserved && buffer.end == 1024 * sizeof(uint32_t) && buffer.use_heap == 1 &&
              storage[0] == 0x12345678 && storage[1] == 0xfeedface,
          "growing borrowed storage preserves its owner and every instruction");
    std::free(buffer.data);
}

void owned_growth(bool heap) {
    AssemblerBuffer buffer{nullptr, 0, 0, heap ? 1U : 0U};
    constexpr uint32_t count = 32769;
    for (uint32_t i = 0; i < count; ++i) {
        buffer.emit(i ^ 0x9e3779b9);
    }
    bool preserved = buffer.end == count * sizeof(uint32_t);
    for (uint32_t i = 0; i < count; ++i) {
        preserved &= buffer.data[i] == (i ^ 0x9e3779b9);
    }
    check(preserved, heap ? "heap growth preserves every instruction"
                          : "mapped growth preserves every instruction");
    if (heap) {
        std::free(buffer.data);
    } else {
        munmap(buffer.data, buffer.end_cap);
    }
}

void capacity_overflow() {
    AssemblerBuffer buffer{nullptr, 0, UINT64_C(1) << 63, 1};
    bool rejected = false;
    try {
        buffer.grow();
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    check(rejected && buffer.data == nullptr && buffer.end == 0 &&
              buffer.end_cap == (UINT64_C(1) << 63) && buffer.use_heap == 1,
          "unrepresentable growth fails before changing storage");
    std::free(buffer.data);
}

void allocation_failure(bool heap) {
    // Four exbibytes cannot fit in the macOS user address space. Under ASan,
    // run with allocator_may_return_null=1 to exercise the allocator's result.
    AssemblerBuffer buffer{nullptr, 0, UINT64_C(1) << 61, heap ? 1U : 0U};
    bool rejected = false;
    try {
        buffer.grow();
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    check(rejected && buffer.data == nullptr && buffer.end == 0 &&
              buffer.end_cap == (UINT64_C(1) << 61) && buffer.use_heap == (heap ? 1U : 0U),
          heap ? "failed heap allocation leaves the buffer unchanged"
               : "failed mapping leaves the buffer unchanged");
}
}  // namespace

int main() {
    exact_capacity();
    borrowed_growth();
    owned_growth(true);
    owned_growth(false);
    capacity_overflow();
    allocation_failure(true);
    allocation_failure(false);
    return failures != 0;
}
