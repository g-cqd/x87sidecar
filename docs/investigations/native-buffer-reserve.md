# Native buffer reserve contract

Rosetta's native allocator ownership differs from the C++ replicas in
`rosetta_core`. In the audited native runtime, a nonzero `use_heap` flag selects
an arena. Native grow does not free the previous arena allocation. Our local
`AssemblerBuffer` instead owns its `calloc`/`free` replacements.

Reserve executes on the requesting parent thread inside the existing translation
call. It uses stock grow or the stock arena allocator, then retries translation.
The sidecar does not modify the native arena. Allocation failure retains stock's
fatal behavior; these functions do not throw through the injected assembly stub.

## Audited images

The audit used these installed arm64 images on 2026-09-26:

- `/usr/libexec/rosetta/runtime`: UUID
  `0157CD23-8C6F-3A23-879A-AB22483C678C`.
- `/Library/Apple/usr/libexec/oah/libRosettaRuntime`: UUID
  `0406CBB0-9E78-3F79-8D84-F40F9E41332C`, export version
  `0x16f0200000000`.

Addresses below are image-relative offsets in these images.

## Parent allocation

The libRosettaRuntime emitter at `0x18a0` calls grow at `0x30b4`. Grow doubles
capacity, starting at 16 KiB. Nonzero `use_heap` loads the arena pointer from
`0x6adf8` and calls `0x4c6c4`. Mode zero calls the mapping helper at `0x4c764`.
Grow copies the old capacity and unmaps the old allocation only for mode zero.
Callers must therefore validate the capacity as well as the live prefix.

The arena allocator rounds to 16-byte alignment and advances a plain shared bump
pointer. Its slow path maps at least 2 MiB. It has no internal lock. The mapping
helper calls the mmap syscall stub and routes failure to `0x4ed7c`, whose
diagnostic path ends in `BRK`.

`discoverRuntimeAllocator` checks complete emitter, grow, arena, mapping and fatal
function shapes, their call relationships, syscall stubs and the arena pointer's
writable-data range. It records the exact bytes for live-image verification.
Untouched list-shaped fields remain opaque: the observed `field_B0` records are
16 bytes, whereas our two appended relocation lists contain 12-byte `Fixup`s.

## External serialization

The runtime's only direct call to its `translator_translate` import thunk
(`0x1fa0`) is at `0x90f0`, inside the function beginning at `0x8ce4`. Its entry
acquires two global locks through `0x1a5d0`:

| Call site | Lock address |
| --- | --- |
| `0x8d44` | `0x3bab0` |
| `0x8d50` | `0x3b9d0` |

Both locks remain held at `0x90f0`. Matching unlocks at `0x9c8c` and `0x9c98`
occur after translation. Lock uses `CASA` acquire at `0x1a5e8`; unlock uses
`SWPL` release at `0x1a6a4`. The requesting thread retains these locks while the
hook calls native reserve.

`discoverAllocatorCaller` checks SHA-256 fingerprints of the entire audited
caller (`0xfdc` bytes), lock/unlock implementation (`0x140` bytes), and import
thunk (`16` bytes). It resolves acquire/release and translator call edges,
verifies the import name, rejects additional direct translator callers, and
records the complete ranges for live-byte checks. A changed caller, lock body,
or import layout disables hooking until re-audited. This intentionally restricts
runtime compatibility instead of assuming a new build preserves serialization.

## Verification boundary

Native regressions mutate ownership, alignment, call targets, failure returns,
lock acquisition, acquire semantics, import identity and caller count. They also
exercise truncated ranges, overflow, relocation and ambiguous candidates. ASan
and UBSan cover these discovery paths.

Installation checks do not protect against arbitrary code mutation after the
hook is installed. Mach writes to unused output tails are not atomic remote
transactions. Native reserve keeps allocator mutations in the parent; translation
lengths are published by the final result write.

## Bounded reply protocol

A fitting translation publishes its complete result in one request. An output
that needs growth returns `Reserve` with all three required capacities, without
publishing output bytes or speculative register-cache state. The parent validates
the entire reply and old buffer metadata before any allocation, reserves through
the checked native functions, and retries the exact original request once. The
retry keeps the full IR block and profile key. It adds one IPC round trip and
repeats translation; it does not change emitted instruction boundaries.

`None` permits stock fallback only for a recognized real opcode. Synthetic ARPL
has no stock implementation: local allocation failure or failed publication
returns `Fatal` with its opcode instead. An invalid request that cannot be safely
identified also returns `Fatal`. Both paths invalidate the sidecar register
cache. The stub prints a diagnostic and exits with status 137 rather than
execute an instruction stock cannot translate. This correctness requirement
also applies to the diagnostic `loader_always_none` mode, which now performs an
allocation-free two-byte opcode read before deciding whether stock is safe.

Malformed replies, IPC errors, invalid continuation indices, and a second
`Reserve` also terminate the parent. They cannot silently delegate to stock with
the cache retained from the first reservation. Only a validated `None` reaches
stock, and every such sidecar outcome invalidates the cache.

The native stub regression executes the generated ARM instructions with injected
IPC and allocator callbacks. Invalid old live lengths, capacities, alignment,
ownership, null storage, pointer overflow and committed counts must be rejected
before any allocator callback. Sidecar regressions inject allocation and Mach
publication failures for ARPL and verify that real opcodes retain stock fallback.

## Measured tradeoff

On Apple M1, macOS 27.0 (26A5388g), release arm64 C++23 with `-O3` and ThinLTO,
a Mach-self request fixture compared commit `b235cf1` with this change. Nine
alternating samples each included 10,000 warmup and 10,000 measured requests.
Both revisions used identical parent storage, IR, register masks, cache reset,
and checksum validation. These measurements include translation and Mach memory
operations, but exclude cross-process IPC scheduling.

| Fitting input | Before median (range), ns | After median (range), ns | Emitted ARM words | Mach operations/request |
| --- | --- | --- | --- | --- |
| FLD1 | 5529.28 (5487.20–5676.57) | 5544.93 (5496.94–5570.75) | 892 → 892 | 4 → 4 |
| Relocated FLD | 6271.87 (6234.53–6307.54) | 6238.15 (6220.74–6353.30) | 893 → 893 | 5 → 5 |

Output checksums remained `1a47beab3d65734d` and `ec64e47a534b5f37`, respectively.
The overlapping timings do not establish a fitting-path speedup.

A separate nine-sample comparison within the new implementation measured a
relocated FLD with fitting storage against zero instruction capacity followed by
reserve/retry. Median request processing rose from 6436.55 ns to 9498.89 ns;
ranges were 6207.14–7656.73 ns and 9278.29–12493.00 ns. Both emitted the same
893 words and checksum. Mach operations rose from five to seven. This fixture
supplied already allocated backing storage after `Reserve`; it excludes native
parent allocation and the additional IPC round trip. It measures the cost of
repeating translation, not end-to-end growth latency.

Instruments Allocations recorded the constant-load fixture, including both
10,000-request phases. Both revisions recorded 21,002 total heap allocations and
327,803,104 cumulative allocated bytes, including launch/instrumentation costs.
Both recorded 20,000 transient 16 KiB allocations (327,680,000 cumulative bytes).
These are allocation totals, not peak memory. Fitting-request allocation traffic
is unchanged; the local scratch allocation remains a separate optimization
opportunity.

The completed matrix passed 1026 checks with zero failures and two known stock
Rosetta divergences (1028 total). It includes ARPL, signal storm, geometry replay,
tracing and compatibility. Native request, generated-stub and allocator-discovery
regressions also passed under AddressSanitizer and UndefinedBehaviorSanitizer.
