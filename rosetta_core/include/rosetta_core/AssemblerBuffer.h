#pragma once

#include <cassert>
#include <cstdint>

struct AssemblerBuffer {
    // Local-only ownership marker; never write this value back to Rosetta.
    static constexpr uint32_t kBorrowedHeap = 2;

    uint32_t* data;     // +0x00: pointer to buffer
    uint64_t end;       // +0x08: byte offset of next write position
    uint64_t end_cap;   // +0x10: byte offset of end of allocated capacity
    uint32_t use_heap;  // +0x18: 0 = mmap, 1 = owned heap, 2 = borrowed until growth

    // Appends one instruction. Storage is word-aligned and end <= end_cap.
    // Allocation failure leaves the buffer unchanged and throws std::bad_alloc.
    void emit(uint32_t value) {
        assert(end <= end_cap && end % sizeof(uint32_t) == 0 && end_cap % sizeof(uint32_t) == 0);
        if (end_cap - end < sizeof(uint32_t))
            grow();
        data[end / sizeof(uint32_t)] = value;
        end += sizeof(uint32_t);
    }

    // Preserves live bytes, releases owned storage, and adopts the replacement.
    // Throws std::bad_alloc without mutation if the capacity cannot be doubled
    // or allocation fails. Borrowed storage remains owned by its caller.
    void grow();
};
