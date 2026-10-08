# Rosetta 2 resumes one byte late after two stores rewrite the next instruction

Found while running a commercial game's copy protection under Wine on Apple silicon, then reduced to a plain
x86-64 command-line program with no Wine involved.

## The defect

An x86-64 program builds its own next instruction in an executable page with two successive stores (a `mov word`
then an `xor word` on the same two bytes) and falls through into them. Under Rosetta 2 the process resumes one byte
after the start of the new instruction and decodes garbage from its second byte (here `mov [moffs64], al`, whose
operand is the 64-bit absolute address formed by the following bytes). One store works; two stores fail, with any gap of
NOPs between the stores and the built instruction (0, 1, 8, 64 tested), also in a fresh process and in round 1.
This is Rosetta behaviour, not Wine's: `rosetta_smc` fails the same way natively.

Stub used (offsets in bytes):

```
 0: 53                             push rbx
 1: 66 C7 05 09 00 00 00 90 F0     mov word [rip+9], 0xF090     ; stores 90 F0 at offset 19
10: 66 81 35 00 00 00 00 9F 52     xor word [rip+0], 0x529F     ; 0xF090 ^ 0x529F = 0xA20F -> 0F A2 (cpuid)
19: 00 00                          becomes 0F A2 = cpuid, the very next instruction
21: 5B C3                          pop rbx ; ret
```

## Files

| file | what |
|---|---|
| `rosetta_smc.c` | minimal single-file reproducer (`clang -arch x86_64`), arguments: NOP gap, mode (1 = one store), rounds |
| `smc2-full-variants.c` | the larger harness (PE via mingw and native Mach-O from one source): the exact NFS16 page bytes, single-step, exec-guard and W^X variants, spinning threads |
| `output-minimal.txt` | outputs of the minimal reproducer on the study machine (M1, macOS 27.2, build 26B5091g) |

## Results

* Minimal reproducer: 2000 of 2000 rounds fault at page+0x14 (CPUID at page+0x13), fault address `0xc35b`; one store: 2000/2000 ok.
* Larger harness, natively and under Wine: 100 % fault with spinning threads, `FlushInstructionCache`, fresh pages, the fixed address
  0x1B30000, the trap-flag emulation and the x87 sidecar environment.
* Mitigations that remove it: single-stepping the page (about 1.8 ms per round), exec-guard, and write-xor-execute emulation (read+execute page,
  write fault flips it writable, one store single-stepped, flip back; about 72 microseconds per store while protected).

## Where it is worked around

* g-cqd/wine patches 0008-0010 (`WINE_RWX_WX_EMULATION=1`): W^X emulation for private RWX pages on the Rosetta route; see
  `docs/rosetta-self-modifying-code.md` on the branches `nfs2015-runtime-0010` and `rosetta-smc-candidate`.
* g-cqd/nfs-macos `docs/ROSETTA-SMC.md`: the app turns it on by default for Need for Speed (2015) and logs the stub page.
* rosetta3 (our own translator) handles the same stub exactly: stores into code end the block and the store is replayed.

## Not verified

Other chips or macOS versions; whether Apple has fixed it; the exact internal cause (the translate-ahead explanation is an inference).
