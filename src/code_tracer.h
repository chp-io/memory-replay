#ifndef CODE_TRACER_H
#define CODE_TRACER_H

#define CODE_TRACER_BUFFER_SIZE 128ul
#define CODE_TRACER_MAP_SIZE (1ul << 16) //64kb
#define CODE_TRACER_CF_LIMIT 20ul

typedef struct code_tracer code_tracer_t;
typedef void (*code_tracer_callback_t)(code_tracer_t *code_tracer);

typedef struct code_tracer {
    // private
    vmi_t *vmi;
    code_tracer_callback_t cb; // optional callback to issue when a new path is triggered

    bool *stop;

    addr_t prev_loc;
    addr_t next_cf;
    addr_t next_cf_pa;
    addr_t points;

    uint8_t backup;

    // public
    uint8_t *map;

    unsigned long counter;
    int error;
} code_tracer_t;

code_tracer_t *code_tracer_setup(vmi_t *vmi, uint8_t *map, bool *stop);
bool code_tracer_start(code_tracer_t *code_tracer, addr_t pagetable, addr_t start);
void code_tracer_close(code_tracer_t *code_tracer);

#endif
