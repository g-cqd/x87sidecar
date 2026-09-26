#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../rosetta_loader/src/macho_loader.hpp"
#include "../rosetta_loader/src/offset_finder.hpp"

namespace {
int failures = 0;
void check(bool passed, const char* name) {
    std::printf("%s  %s\n", passed ? "PASS" : "FAIL", name);
    failures += !passed;
}
}  // namespace

int main() {
    MachoLoader loader;
    if (!loader.open("/Library/Apple/usr/libexec/oah/libRosettaRuntime")) {
        check(false, "stock runtime is available for allocation ABI validation");
        return 1;
    }
    const auto* text = loader.getSection("__TEXT", "__text");
    const auto* data = loader.getSection("__DATA", "__bss");
    if (!text || !data) {
        check(false, "stock runtime exposes code and writable allocator data");
        return 1;
    }
    const auto discover = [&](const std::vector<std::uint8_t>& image) {
        return discoverRuntimeAllocator(image, text->offset, text->offset + text->size, data->addr,
                                        data->addr + data->size);
    };
    const auto found = discover(loader.buffer_);
    check(found.grow != 0 && found.arenaAllocate != 0 && found.arenaPointer != 0 &&
              !found.codeChecks.empty(),
          "stock instruction growth and arena ABI are discovered together");
    if (found.grow == 0) {
        return 1;
    }
    auto modified = loader.buffer_;
    // Changing the mode-dependent release changes the allocator contract.
    modified[found.grow + 0x6c] ^= 1;
    check(discover(modified).grow == 0, "changed buffer ownership branch disables native calls");
    modified = loader.buffer_;
    modified[found.arenaAllocate + 0x14] ^= 1;
    check(discover(modified).grow == 0, "changed arena alignment disables native calls");
    modified.resize(found.grow + 16);
    check(discover(modified).grow == 0, "truncated allocation code fails closed");
    check(discoverRuntimeAllocator(loader.buffer_, 0, UINT64_MAX, 0, UINT64_MAX).grow == 0,
          "overflowing section ranges fail closed");
    check(discoverRuntimeAllocator(loader.buffer_, text->offset + 1, text->offset + text->size,
                                   data->addr, data->addr + data->size)
                  .grow == 0,
          "misaligned executable section fails closed");
    check(discoverRuntimeAllocator(loader.buffer_, text->offset, text->offset + text->size,
                                   found.arenaPointer + 8, data->addr + data->size)
                  .grow == 0,
          "allocator pointer outside writable data fails closed");
    std::vector<std::uint8_t> relocated(4096, 0);
    relocated.insert(relocated.end(), loader.buffer_.begin(), loader.buffer_.end());
    const auto moved =
        discoverRuntimeAllocator(relocated, text->offset + 4096, text->offset + text->size + 4096,
                                 data->addr + 4096, data->addr + data->size + 4096);
    check(moved.grow == found.grow + 4096 && moved.arenaAllocate == found.arenaAllocate + 4096 &&
              moved.arenaPointer == found.arenaPointer + 4096,
          "allocation discovery follows relocated PC-relative calls and data");
    modified = loader.buffer_;
    const auto& emitter = found.codeChecks.front();
    const std::uint64_t duplicate = text->offset + text->size;
    modified.resize(std::max<std::size_t>(modified.size(), duplicate + emitter.bytes.size()));
    std::memcpy(modified.data() + duplicate, emitter.bytes.data(), emitter.bytes.size());
    const std::uint32_t duplicateCall =
        0x94000000 |
        ((static_cast<std::int64_t>(found.grow) - static_cast<std::int64_t>(duplicate + 0x28)) / 4 &
         0x03ffffff);
    std::memcpy(modified.data() + duplicate + 0x28, &duplicateCall, sizeof(duplicateCall));
    check(discoverRuntimeAllocator(modified, text->offset, duplicate + emitter.bytes.size(),
                                   data->addr, data->addr + data->size)
                  .grow == 0,
          "ambiguous complete allocator call chains fail closed");
    modified = loader.buffer_;
    const std::uint32_t escapedCall = 0x94000000 | 0x01ffffff;
    std::memcpy(modified.data() + found.grow + 0x3c, &escapedCall, sizeof(escapedCall));
    check(discover(modified).grow == 0, "out-of-section arena call target fails closed");
    modified = loader.buffer_;
    // The fatal function is the only 68-byte snapshot in this contract.
    for (const auto& proof : found.codeChecks) {
        if (proof.bytes.size() == 68) {
            const std::uint32_t returning = 0xd65f03c0;
            std::memcpy(modified.data() + proof.offset + 64, &returning, sizeof(returning));
        }
    }
    check(discover(modified).grow == 0, "returning allocation-failure helper fails closed");
    for (const auto& proof : found.codeChecks) {
        check(proof.offset <= loader.buffer_.size() &&
                  proof.bytes.size() <= loader.buffer_.size() - proof.offset &&
                  std::memcmp(proof.bytes.data(), loader.buffer_.data() + proof.offset,
                              proof.bytes.size()) == 0,
              "native code snapshot exactly matches its validated image range");
    }
    MachoLoader runtime;
    if (!runtime.open("/usr/libexec/rosetta/runtime")) {
        check(false, "runtime caller image is available");
        return 1;
    }
    const auto* runtimeText = runtime.getSection("__TEXT", "__text");
    const auto caller = discoverAllocatorCaller(runtime.buffer_, runtimeText->offset,
                                                runtimeText->offset + runtimeText->size);
    check(caller.size() == 3, "both translation locks cover the native allocation hook");
    if (!caller.empty()) {
        auto broken = runtime.buffer_;
        // Bypass the second lock acquisition while preserving the surrounding code.
        const std::uint32_t nop = 0xd503201f;
        std::memcpy(broken.data() + caller.front().offset + 0x6c, &nop, sizeof(nop));
        check(discoverAllocatorCaller(broken, runtimeText->offset,
                                      runtimeText->offset + runtimeText->size)
                  .empty(),
              "removing a caller lock disables native reserve");
        broken = runtime.buffer_;
        broken[caller[1].offset + 0x18] ^= 1;
        check(discoverAllocatorCaller(broken, runtimeText->offset,
                                      runtimeText->offset + runtimeText->size)
                  .empty(),
              "changing acquire semantics disables native reserve");
        broken = runtime.buffer_;
        broken[caller[2].offset + 8] ^= 1;
        check(discoverAllocatorCaller(broken, runtimeText->offset,
                                      runtimeText->offset + runtimeText->size)
                  .empty(),
              "changing the imported translator disables native reserve");
        broken = runtime.buffer_;
        const auto extraCallSite = runtimeText->offset;
        const std::uint32_t extraCall =
            0x94000000 |
            ((static_cast<std::int64_t>(caller[2].offset) - extraCallSite) / 4 & 0x03ffffff);
        std::memcpy(broken.data() + extraCallSite, &extraCall, sizeof(extraCall));
        check(discoverAllocatorCaller(broken, runtimeText->offset,
                                      runtimeText->offset + runtimeText->size)
                  .empty(),
              "an additional unaudited translator caller disables native reserve");
        broken = runtime.buffer_;
        constexpr char name[] =
            "__ZN7rosetta7runtime7library20translator_translateEPKNS1_"
            "12ModuleResultE15TranslationMode";
        const auto nameAt = std::search(broken.begin(), broken.end(), name, name + sizeof(name));
        if (nameAt == broken.end()) {
            check(false, "translator import name exists in the verified image");
        } else {
            *nameAt ^= 1;
            check(discoverAllocatorCaller(broken, runtimeText->offset,
                                          runtimeText->offset + runtimeText->size)
                      .empty(),
                  "a renamed import cannot reuse the audited translator thunk");
        }
    }
    return failures != 0;
}
