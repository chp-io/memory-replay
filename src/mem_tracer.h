#ifndef MEM_TRACER_H
#define MEM_TRACER_H

typedef struct page_access_log page_access_log_t;
typedef struct mem_tracer mem_tracer_t;
typedef void (*mem_tracer_callback_t)(mem_tracer_t *mem_tracer, page_access_log_t *page, addr_t va);

typedef struct page_access_log {
    addr_t pa;
    unsigned long r_count;
    unsigned long rw_count;
    GList *log; // list of two-dim arrays, with pre/post access values
} page_access_log_t;

typedef struct mem_tracer {
    // private
    event_callback_t mem_cb;
    GSList *reset_pages; // list of page_access_log_t

    vmi_t *vmi;
    mem_tracer_callback_t cb; // optional callback to issue when a page is accessed
    vmi_event_t *event; // the currently active event
    void *data;
    bool *stop;

    // public
    GHashTable *pages; // table of page_access_log_t, key: pa
    GHashTable *values; // table of access counts, key: value
    int error;
} mem_tracer_t;

/*
 * Setup the mem_tracer infrastructure, initialize and register events.
 * Callback is optional, issued before a memory page is accessed
 */
mem_tracer_t *mem_tracer_setup(vmi_t *vmi, mem_tracer_callback_t cb, void* data);

/*
 * Change EPT access permissions on user pages in target pagetable
 */
void mem_tracer_trap_user_pages(mem_tracer_t *mem_tracer, addr_t pagetable);

/*
 * Print out collected memaccess values and location information
 */
void mem_tracer_print_stats(mem_tracer_t *mem_tracer);

/*
 * Free mem_tracer infrastructure, optionally reset EPT access permissions
 */
void mem_tracer_close(mem_tracer_t *mem_tracer, bool remove_traps);

#endif
