#ifndef OS_LOADER_H
#define OS_LOADER_H

#include "os_types.h"

#define MAGIC_ATKB 0x424B5441  /* "ATKB" */
#define MAGIC_ATKM 0x4D4B5441  /* "ATKM" */

#define MODULE_ALLOC_BASE  0x41000000ULL
#define MODULE_ALLOC_LIMIT 0x4F000000ULL

typedef struct {
    uint32_t module_id;
    uint32_t offset;
    uint32_t size;
} bundle_index_t;

typedef struct {
    uint32_t magic;
    uint32_t cartridge_count;
    bundle_index_t index[ 8 ];
} bundle_header_t;

typedef struct {
    uint32_t magic;
    uint32_t module_id;
    uint32_t code_size;
    uint32_t text_size;
    uint32_t bss_size;
    uint32_t stack_size;
    uint8_t  signature[ 64 ];
} cartridge_header_t;

typedef struct {
    uint32_t module_id;
    uint64_t code_base;
    uint64_t code_size;
    uint64_t stack_base;
    uint64_t stack_size;
} module_region_t;

extern module_region_t module_regions[ 8 ];
extern uint32_t loaded_module_count;

/* FIX: Return type changed from module_region_t* to int to match loader.c and syscall.c */
int  get_region_for_current_module(void);
void loader_init(void);

#endif