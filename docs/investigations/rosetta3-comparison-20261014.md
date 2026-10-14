# x87sidecar vs. rosetta3 vs. stock Rosetta 2: an independent comparison (2026-10-14)

Full report, raw data, and reproduction commands:
`/Users/gc/Developer/rosetta3/docs/benchmarks-rosetta3-vs-rosetta2-vs-x87sidecar-20261014.md` (lives in the
rosetta3 tree since that is where the comparison was commissioned; cross-linked here since it measures this
project too).

## Summary for this project

Four new, independently written workloads (not reused from `benchmarks/` or from rosetta3's own kernels), run
under stock Rosetta 2, this project's default-attach JIT, and rosetta3's translator, on the same Apple M2 Pro,
back to back:

| workload | x87sidecar vs. stock Rosetta 2 | x87sidecar vs. rosetta3 |
|---|---:|---:|
| x87 compare+branch (`fcom`/`fnstsw`/`sahf`) | 5.47x faster | 1.43x faster |
| mixed x87/SSE multiply-add via memory | 18.2x faster (noisy baseline, see full report) | 1.91x faster |
| x87 arithmetic on denormals, SSE FTZ/DAZ set | 9.5x faster | 1.20x faster |
| Mandelbrot (SSE2 only, no x87) | no measurable difference (expected: this project only touches x87) | not comparable (see full report's cold/warm caveat) |

x87sidecar is consistently somewhat faster than rosetta3 on every x87-bearing workload tested, including the
mixed x87/SSE case where rosetta3's single-pass lowering of both instruction sets might have been expected to
have an edge; it did not, in this test. Both are far ahead of stock Rosetta 2 on x87-heavy code, and the
Mandelbrot check confirms this project changes nothing for code that never touches x87, as designed.

Run in this project's own default-attach mode (`x87sidecar_entitled`, the same mode `scripts/run_benchmarks.sh`
uses), not `--cooperative` -- the comparison binaries are plain C programs with no Mach-port handshake, and
`--cooperative` requires the target to perform one. See the full report's "Threats to validity" section for why
that is a disclosed, deliberate choice rather than an oversight, and for every other caveat (shared-machine
noise, no real game binary available, 32-bit-vs-64-bit scope, single opt level).

## A gap found while writing the comparison, not by this project's own test suite

One of the test loops happened to use `fldpi` (load the constant pi) next to the x87 comparison it meant to
measure. Under rosetta3's translator that made the whole loop run through rosetta3's interpreter instead of its
JIT, on every iteration, not just the first -- see the full report's "A gap this benchmark found" section for
the exact `Stats` evidence. That is a rosetta3 issue, not an x87sidecar one, but it is the kind of finding this
project's own `--probe`, hash-based stock-fallback list (`X87_STOCK_HASH_LIST`), and `KNOWN_STOCK_DIVERGENCE`
mechanisms exist to catch on this project's own side of a translation boundary -- worth keeping in mind if
x87sidecar and rosetta3 ever need to agree on a shared compatibility list.
