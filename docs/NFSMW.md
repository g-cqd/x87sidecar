# NFSMW native-boundary experiment

This branch starts at athei/x87sidecar 4048fcf436876f26c799ba2fa340ec73f4cca95e. It contains the source used for the local NFSMW macOS preview on 2026-09-26.

Cache empty x87 tags during native-state conversion and use guarded fast paths for signed zero and exactly representable binary64 normal values. Keep general conversion for other values. Tests cover boundary exponents, discarded mantissa bits, signed zero, exceptional values, tags and preserved flags. The native-boundary benchmark source is included.

## Measured trade-off

Apple M1, macOS 27 beta; 21 samples per build and one million iterations per sample. Mean boundary times in milliseconds:

| Input | Upstream | Patched |
| --- | ---: | ---: |
| Normal | 27.956 | 22.225 |
| Zero | 26.896 | 19.265 |
| Mixed | 25.842 | 20.205 |
| Special values | 23.451 | 25.710 |

The special-value case became slower. These are isolated boundary measurements, not an FPS guarantee. Native-state, arithmetic, conversion, flag-preservation and signal-context/storm checks passed. This patch does not change the project's existing arithmetic precision model or make all x87 arithmetic equivalent to native 80-bit hardware.

Build using the upstream CMake instructions. The additional benchmark target is bench_native_boundary.
