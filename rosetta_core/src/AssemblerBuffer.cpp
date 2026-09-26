#include "rosetta_core/AssemblerBuffer.h"

#include <sys/mman.h>

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

void* mmap_anonymous_rw(size_t size, int tag) {
    void* result;  // x0

    result = mmap(reinterpret_cast<void*>(0x100000000LL), size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, tag << 0x18, 0);
    return result;
}

void AssemblerBuffer::grow() {
    assert(end <= end_cap);
    // Keep both the byte count and pointer differences representable.
    constexpr auto max_capacity = static_cast<uint64_t>(PTRDIFF_MAX);
    if (end_cap > max_capacity / 2) {
        throw std::bad_alloc();
    }
    uint64_t new_cap = (end_cap == 0) ? 0x4000 : end_cap * 2;

    uint32_t* new_data;
    if (this->use_heap) {
        new_data = static_cast<uint32_t*>(calloc(1, new_cap));
    } else {
        // Original uses flags 0xE6 = MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT (macOS)
        new_data = static_cast<uint32_t*>(mmap_anonymous_rw(new_cap, 0xE6));
    }
    if (new_data == nullptr || new_data == MAP_FAILED) {
        throw std::bad_alloc();
    }

    // Only [0, end) contains live instructions. The sidecar's vector is borrowed;
    // after the first growth, every replacement is owned by this buffer.
    if (end != 0) {
        memcpy(new_data, data, end);
    }
    if (data != nullptr) {
        if (use_heap == 0) {
            munmap(data, end_cap);
        } else if (use_heap != kBorrowedHeap) {
            free(data);
        }
    }

    data = new_data;
    end_cap = new_cap;
    if (use_heap != 0) {
        use_heap = 1;
    }
}
