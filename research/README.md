# research/

Probe programs written for the Rosetta study on branch `fc2-rosetta-research`.
Everything here is our own code. Nothing in this directory contains Apple
binaries, extracted code or disassembly; Apple code is referred to only by
offset in the notes that live outside the repository.

| directory | what it is |
|---|---|
| `probes/` | x86_64 and arm64 micro-benchmarks, same dependency structure on both sides (Rosetta vs native cost per instruction class) |
| `gaps_a/` | cpuid, privileged instructions, 32-bit LDT, address space, signals, atomics and TSO litmus probes |
| `gaps_b/` | x87 fidelity, EFLAGS sweep, partial registers, string ops, self-modifying code and per-operation cost probes |
| `rep/` | harness and shapes used to test whether a cheaper `rep movs/stos` lowering would pass Rosetta's state-recovery classifier (it would not; see the notes) |
| `misc/` | `translated_hello.c` (shows `sysctl.proc_translated`), `ordcost*.c` (cost of acquire/release ordering on arm64 without hardware TSO) |

All probes only run ordinary command-line programs; none needs root or changes
a system setting.
