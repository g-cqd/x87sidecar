# Probe build: x86_64 (runs under Rosetta) and native arm64. Output in build/ (untracked).
CC ?= clang
B := build
all: $(B)/probes_x86 $(B)/probes_arm64
$(B)/probes_x86: driver.c probes_x86.S probes.def
	@mkdir -p $(B)
	$(CC) -arch x86_64 -O2 -mno-red-zone -o $@ driver.c probes_x86.S
$(B)/probes_arm64: driver.c probes_arm64.S probes.def
	@mkdir -p $(B)
	$(CC) -arch arm64 -O2 -o $@ driver.c probes_arm64.S
clean:
	rm -rf $(B)
kernels: $(B)/k_vec_x86_64 $(B)/k_vec_arm64 $(B)/k_scalar_x86_64 $(B)/k_scalar_arm64
$(B)/k_vec_%: kernels.c
	@mkdir -p $(B)
	$(CC) -arch $* -O2 -ffp-contract=off -o $@ kernels.c
$(B)/k_scalar_%: kernels.c
	@mkdir -p $(B)
	$(CC) -arch $* -O2 -ffp-contract=off -fno-vectorize -fno-slp-vectorize -o $@ kernels.c
