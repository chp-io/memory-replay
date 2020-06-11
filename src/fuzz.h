#ifndef FUZZ_H
#define FUZZ_H

uint64_t create_fork_and_fuzz(uint64_t domid, uint32_t vcpus, uint8_t *map, GHashTable *values, addr_t cr3, addr_t rip, addr_t pa, addr_t va);

#endif
