#include "private.h"

void mem_tracer_print_stats(mem_tracer_t *mem_tracer)
{
    GList *offsets = g_hash_table_get_keys(mem_tracer->pages);
    offsets = g_list_sort(offsets, gint64_compare);
    unsigned long r_total = 0, rw_total = 0;

    GList *offset = offsets;
    while ( offset )
    {
        addr_t offset_value = *(addr_t*)offset->data;
        offset = offset->next;

        page_access_log_t *page = g_hash_table_lookup(mem_tracer->pages, &offset_value);

        printf("[0x%lx %03lx] R: %5lu, R/W: %5lu\n", offset_value >> 12, offset_value & 0xfff, page->r_count, page->rw_count);

        page->log = g_list_reverse(page->log);

        GList *loop = page->log;
        while ( loop )
        {
            uint64_t *mem = (uint64_t*)loop->data;

            uint8_t *before = (uint8_t*)&mem[0];
            uint8_t *after = (uint8_t*)&mem[1];

            uint8_t test[9] = { [8] = '\0' };
            memcpy(&test[0], after, 8);

            printf("\t%02x%02x %02x%02x %02x%02x %02x%02x\n", before[0], before[1], before[2], before[3], before[4], before[5], before[6], before[7]);
            printf("\t%02x%02x %02x%02x %02x%02x %02x%02x", after[0], after[1], after[2], after[3], after[4], after[5], after[6], after[7]);


            if ( g_str_is_ascii((const char*)&test) )
                printf(" -> '%s'\n", test);

            printf("\n\n");

            g_free(loop->data);
            loop = loop->next;
        }

        g_list_free(page->log);

        r_total += page->r_count;
        rw_total += page->rw_count;
    }

    g_list_free(offsets);

    printf("--------------------------\n");
    printf("Total R: %5lu    Total R/W: %5lu\n", r_total, rw_total);
    printf("--------------------------\n\n");

    printf("Unique memaccess values: %u\n", g_hash_table_size(mem_tracer->values));
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, mem_tracer->values);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        uint8_t test[9] = { [8] = '\0' };
        memcpy(&test[0], key, 8);

        uint8_t *v = (uint8_t*)key;
        printf("\t[%3d] %02x%02x %02x%02x %02x%02x %02x%02x", GPOINTER_TO_UINT(value), v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);

        if ( g_str_is_ascii((const char*)&test) )
            printf(" -> '%s'", test);

        printf("\n");
    }
}

static event_response_t mem_cb(vmi_instance_t vmi, vmi_event_t *event)
{
    mem_tracer_t *mem_tracer = event->data;
    addr_t pa = (event->mem_event.gfn << 12) + event->mem_event.offset;
    page_access_log_t *page;

    if ( !(page = g_hash_table_lookup(mem_tracer->pages, &pa)) )
    {
        if ( !(page = g_try_malloc0(sizeof(page_access_log_t))) )
        {
            mem_tracer->error = -ENOMEM;
            goto done;
        }

        page->pa = pa;
        g_hash_table_insert(mem_tracer->pages, &page->pa, page);
    }

    mem_tracer->reset_pages = g_slist_prepend(mem_tracer->reset_pages, page);

    if ( event->mem_event.out_access & VMI_MEMACCESS_W )
        page->rw_count++;
    else if ( event->mem_event.out_access & VMI_MEMACCESS_R )
        page->r_count++;

    uint64_t *mem = g_try_malloc0(2*sizeof(uint64_t));
    if ( !mem )
    {
        mem_tracer->error = -ENOMEM;
        goto done;
    }

    vmi_read_64_pa(vmi, page->pa, mem);
    page->log = g_list_prepend(page->log, mem);

    if ( mem_tracer->cb )
    {
        mem_tracer->event = event;
        mem_tracer->cb(mem_tracer, page, event->mem_event.gla);
    }

    vmi_set_mem_event(vmi, pa >> 12, VMI_MEMACCESS_N, 0);

done:
    return VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP;
}

static event_response_t singlestep_cb(vmi_instance_t vmi, vmi_event_t *event)
{
    mem_tracer_t *mem_tracer = event->data;
    GSList *loop = mem_tracer->reset_pages;
    while (loop)
    {
        page_access_log_t *page = loop->data;

        uint64_t *mem = page->log->data;
        vmi_read_64_pa(vmi, page->pa, ++mem);

        guint count = 0;
        gpointer v = g_hash_table_lookup(mem_tracer->values, mem);
        if ( v )
            count = GPOINTER_TO_UINT(v);
        count++;

        g_hash_table_insert(mem_tracer->values, mem, GUINT_TO_POINTER(count));
        vmi_set_mem_event(vmi, page->pa >> 12, VMI_MEMACCESS_RW, 0);
        loop=loop->next;
    }

    g_slist_free(mem_tracer->reset_pages);
    mem_tracer->reset_pages = NULL;

    return VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP;
}

mem_tracer_t *mem_tracer_setup(vmi_t *vmi, mem_tracer_callback_t cb, void *data)
{
    mem_tracer_t *mem_tracer = g_try_malloc0(sizeof(mem_tracer_t));
    if ( !mem_tracer )
        return NULL;

    mem_tracer->pages = g_hash_table_new_full(g_int64_hash, g_int64_equal, NULL, g_free);
    if ( !mem_tracer->pages )
        goto err;

    mem_tracer->values = g_hash_table_new(g_int64_hash, g_int64_equal);
    if ( !mem_tracer->values )
        goto err;

    mem_tracer->vmi = vmi;
    mem_tracer->cb = cb;
    mem_tracer->data = data;

    if ( !enable_callback(vmi, MEMACCESS, mem_cb, mem_tracer) )
        goto err;
    if ( !enable_callback(vmi, SINGLESTEP, singlestep_cb, mem_tracer) )
        goto err;

    return mem_tracer;

err:
    mem_tracer_close(mem_tracer, false);
    return NULL;
}

void mem_tracer_trap_user_pages(mem_tracer_t *mem_tracer, addr_t pagetable)
{
    /*
     * Set memory permissions on process and start memory listener
     */
    vmi_instance_t vmi = mem_tracer->vmi->libvmi;
    GSList *page_list = vmi_get_va_pages(vmi, pagetable);
    printf("Got %u pages in target process' memory at 0x%lx\n", g_slist_length(page_list), pagetable);

    GSList *loop = page_list;
    while ( loop )
    {
        page_info_t *info = (page_info_t*)loop->data;
        loop = loop->next;

        if ( !USER_SUPERVISOR(info->x86_ia32e.pte_value) )
            continue;

        printf("%lx -> %lx\n", info->vaddr, info->paddr);

        //if ( READ_WRITE(info->x86_ia32e.pte_value) )
        {
            vmi_set_mem_event(vmi, info->paddr >> 12, VMI_MEMACCESS_RW, 0);
        }
    }

    g_slist_free_full(page_list, (GDestroyNotify)g_free);
}

void mem_tracer_close(mem_tracer_t *mem_tracer, bool remove_traps)
{
    if ( !mem_tracer )
        return;

    disable_callback(mem_tracer->vmi, MEMACCESS, mem_cb);
    disable_callback(mem_tracer->vmi, SINGLESTEP, singlestep_cb);

    if ( remove_traps && mem_tracer->pages )
    {
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init (&iter, mem_tracer->pages);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            page_access_log_t *page = value;
            vmi_set_mem_event(mem_tracer->vmi->libvmi, page->pa >> 12, VMI_MEMACCESS_N, 0);
        }
    }

    if ( mem_tracer->pages )
        g_hash_table_destroy(mem_tracer->pages);
    if ( mem_tracer->values )
        g_hash_table_destroy(mem_tracer->values);
    g_free(mem_tracer);
}
