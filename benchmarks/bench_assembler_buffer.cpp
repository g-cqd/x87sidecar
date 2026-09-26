// Native translation-storage benchmark; no target process or Rosetta hook.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "bench_timing.h"
#include "rosetta_core/AssemblerBuffer.h"

int main() {
    constexpr unsigned rounds = 32;
    // Initial capacity boundary and a workload spanning several growths.
    for (const uint32_t words : {4096U, 32769U}) {
        std::array<uint64_t, 9> samples{};
        uint64_t checksum = 0;
        uint64_t capacity = 0;
        for (unsigned sample = 0; sample <= samples.size(); ++sample) {
            uint64_t elapsed = 0;
            for (unsigned round = 0; round < rounds; ++round) {
                AssemblerBuffer buffer{nullptr, 0, 0, 1};
                const auto start = bench_now_ns();
                for (uint32_t i = 0; i < words; ++i) {
                    buffer.emit(i ^ 0x9e3779b9);
                }
                elapsed += bench_now_ns() - start;
                checksum += buffer.data[0] + uint64_t{buffer.data[words - 1]};
                capacity = buffer.end_cap;
                std::free(buffer.data);
            }
            if (sample != 0) {
                samples[sample - 1] = elapsed;
            }
        }
        std::sort(samples.begin(), samples.end());
        const double divisor = double{rounds} * words;
        std::printf(
            "words=%u capacity_bytes=%llu median_ns_per_word=%.3f "
            "min_ns_per_word=%.3f max_ns_per_word=%.3f checksum=%llu\n",
            words, static_cast<unsigned long long>(capacity), samples[4] / divisor,
            samples.front() / divisor, samples.back() / divisor,
            static_cast<unsigned long long>(checksum));
    }
}
