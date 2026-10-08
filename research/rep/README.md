# research/rep: can `rep movs/stos` get a cheaper lowering under Rosetta's state recovery?

Result: no exact prototype exists (analysis notes are kept outside the repo).

* `classify_harness.c`: loads a user-supplied local copy of the JIT runtime image into a MAP_JIT region
  of the harness process and calls its state-recovery classifier on raw AArch64 words. Contains no
  Apple code or data; the image path is an argument. `CLASSIFIER_OFFSET` is the 27.2 build's offset.
* `shape_test.py` + `shapes/*.s`: candidate instruction shapes and the verdict the classifier gives.
* `asm2bin.sh`: llvm-mc helper. `gen_fastrep.py`: generator of the rejected prefix-copy candidate.

Build: `clang -O1 -arch arm64 -o classify_harness classify_harness.c`
Run:   `HARNESS=./classify_harness python3 shape_test.py <runtime-copy> <annotated-listing> shapes/*.s`
