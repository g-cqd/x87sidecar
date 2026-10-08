/* Which low addresses can a translated process map, and what already occupies them (vm_region walk). */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
int main(void) {
    mach_vm_address_t a = 0; int n = 0;
    while (n < 12) { mach_vm_size_t sz; vm_region_basic_info_data_64_t i; mach_msg_type_number_t c = VM_REGION_BASIC_INFO_COUNT_64; mach_port_t o;
        if (mach_vm_region(mach_task_self(), &a, &sz, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&i, &c, &o)) break;
        printf("  region %#llx-%#llx prot=%d/%d\n", (unsigned long long)a, (unsigned long long)(a + sz), i.protection, i.max_protection); a += sz; n++; if (a >= 0x100000000ull) break; }
    unsigned long long t[] = {0x4000, 0x8000, 0x10000, 0x20000, 0x100000, 0x400000, 0x1000000, 0x2000000};
    for (unsigned k = 0; k < sizeof t / sizeof *t; k++) { void *p = mmap((void *)t[k], 0x4000, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
        printf("  mmap FIXED @%#llx -> %s\n", t[k], p == MAP_FAILED ? strerror(errno) : "ok"); if (p != MAP_FAILED) munmap(p, 0x4000);
        mach_vm_address_t va = t[k]; kern_return_t kr = mach_vm_allocate(mach_task_self(), &va, 0x4000, 0); printf("     mach_vm_allocate fixed -> kr=%d\n", kr); if (!kr) mach_vm_deallocate(mach_task_self(), va, 0x4000); }
    return 0; }
