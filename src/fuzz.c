#include "private.h"

static bool fuzz_iterate(vmi_t *vmi, uint32_t fork_domid, uint64_t pa, uint64_t *mem, bool *stop)
{
    if ( mem )
    {
        //uint8_t test[9] = "notbeef";
        uint8_t test[9] = { [8] = '\0' };
        memcpy(&test[0], mem, 8);
        //printf("\t Starting test with '%s'\n ", test);

        uint8_t *v = (uint8_t*)mem;
        printf("\t Starting fuzz iteration with: %x%x %x%x %x%x %x%x",
               v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);

        if ( g_str_is_ascii((const gchar*)test) )
            printf(": %s", test);
        printf("\n");

        if ( VMI_FAILURE == vmi_write_64_pa(vmi->libvmi, pa, (uint64_t*)mem) )
            return false;
    }

    loop_exit_condition_t exit_cond = loop_vmi(vmi, 50, stop);

    xc_dominfo_t info = { 0 };
    xc_domain_getinfo(xc, fork_domid, 1, &info);

    vmi_pagecache_flush(vmi->libvmi);

    GTimer *timer = g_timer_new();
    g_timer_start(timer);
    xc_memshr_fork_reset(xc, fork_domid);
    g_timer_stop(timer);

    printf("\t - VM Fork memory used: %lu kbyte\n", info.nr_pages * 4096 / 1024);
    printf("\t - Reset time: %f\n", g_timer_elapsed(timer, NULL));
    printf("\t - Exit condition: %s\n", exit_cond_str(exit_cond));
    printf("\t -------------------\n");

    g_timer_destroy(timer);
    return true;
}

uint64_t create_fork_and_fuzz(uint64_t domid, uint32_t vcpus, uint8_t *map, GHashTable *values, addr_t pagetable, addr_t rip, addr_t pa, addr_t va)
{
    bool stop = false, fork_success;
    uint32_t fork_domid = 0;
    vmi_t *vmi = NULL;
    code_tracer_t *code_tracer = NULL;

    guint size = g_hash_table_size(values);

    GTimer *timer = g_timer_new();
    g_timer_start(timer);

    fork_success = fork_vm(domid, vcpus, &fork_domid);

    g_timer_stop(timer);
    gdouble time = g_timer_elapsed(timer, NULL);
    g_timer_destroy(timer);

    if ( !fork_success )
        goto done;

    printf("[FUZZ] Fork: %u. Creation time: %f. Memory @ 0x%lx -> 0x%lx. CF: 0x%lx. Values: %u\n", fork_domid, time, va, pa, rip, size);

    vmi = setup_vmi(NULL, fork_domid, NULL, true, false);
    if ( !vmi )
        goto done;

    code_tracer = code_tracer_setup(vmi, map, &stop);
    if ( !code_tracer )
        goto done;

    /* Record the baseline execution first without touching anything */
    if ( !size )
    {
        printf("[FUZZ] Starting baseline execution to setup coverage map\n");

        if ( code_tracer_start(code_tracer, pagetable, rip) )
            fuzz_iterate(vmi, fork_domid, 0, NULL, &stop);

        goto done;
    }

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init (&iter, values);

    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        if ( !code_tracer_start(code_tracer, pagetable, rip) )
            break;
        if ( !fuzz_iterate(vmi, fork_domid, pa, key, &stop) )
            break;
    }

done:
    code_tracer_close(code_tracer);
    close_vmi(vmi);

    if ( fork_domid )
        xc_domain_destroy(xc, fork_domid);

    return 0;
}
