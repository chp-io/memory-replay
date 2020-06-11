#include "private.h"

typedef struct shredder {
    GHashTable *processes;
    vmi_pid_t target_pid;
    addr_t target_cr3;
    addr_t magic_string;

    bool manual;
    bool harness_bp;
    bool stop;

    vmi_t *vmi;
    mem_tracer_t *mem_tracer;

    uint8_t *map;
} shredder_t;

gint gint64_compare(gconstpointer ptr_a, gconstpointer ptr_b)
{
    gint64 a = *(gint64*)ptr_a;
    gint64 b = *(gint64*)ptr_b;

    if ( a > b )
        return 1;
    if ( a == b )
        return 0;
    return -1;
}

static void find_existing_processes(shredder_t *shredder)
{
    vmi_instance_t vmi = shredder->vmi->libvmi;
    GHashTable *processes = shredder->processes;

    addr_t current_list_entry = 0, next_list_entry = 0, start_entry = 0;
    addr_t linkedlist_offset, name_offset, pid_offset;
    bool looped = 0;

    if ( VMI_OS_LINUX == os )
    {
        if ( VMI_FAILURE == vmi_get_kernel_struct_offset(vmi, "task_struct", "tasks", &linkedlist_offset) )
            return;
        if ( VMI_FAILURE == vmi_get_kernel_struct_offset(vmi, "task_struct", "comm", &name_offset) )
            return;
        if ( VMI_FAILURE == vmi_get_kernel_struct_offset(vmi, "task_struct", "pid", &pid_offset) )
            return;
        if ( VMI_FAILURE == vmi_translate_ksym2v(vmi, "init_task", &next_list_entry) )
            return;

        next_list_entry += linkedlist_offset;
        start_entry = next_list_entry;
    }

    do {

        if ( !next_list_entry )
            return;

        if ( next_list_entry == start_entry && looped )
            return;

        current_list_entry = next_list_entry;
        looped = true;

        addr_t process = current_list_entry - linkedlist_offset;

        if (VMI_FAILURE == vmi_read_addr_va(vmi, current_list_entry, 0, &next_list_entry))
            return;

        uint16_t pid;
        if ( VMI_FAILURE == vmi_read_16_va(vmi, process + pid_offset, 0, &pid) )
            continue;

        gint *_pid = g_try_malloc0(sizeof(gint));
        *_pid = pid;

        if ( !g_hash_table_insert(processes, _pid, GSIZE_TO_POINTER(1)) )
            return;

#if 0
        char *name = vmi_read_str_va(vmi, process + name_offset, 0);
        printf("Found process [%u] %s\n", pid, name);
        free(name);
#endif
    } while ( 1 );
}

static event_response_t int3_cb(vmi_instance_t vmi, vmi_event_t *event)
{
    shredder_t *shredder = event->data;

    gint pid;
    event->interrupt_event.reinject = 1;

    if ( VMI_FAILURE == vmi_dtb_to_pid(vmi, event->x86_regs->cr3, (vmi_pid_t*)&pid) )
    {
        printf("Can't find PID for CR3: 0x%lx\n", event->x86_regs->cr3);
        return 0;
    }

    if ( !shredder->target_pid )
    {
        shredder->target_pid = pid;
        shredder->target_cr3 = event->x86_regs->cr3;
    }

    printf("INT3 in target pid! [%u] CR3: 0x%lx. SLAT: %u\n", pid, event->x86_regs->cr3, event->slat_id);

    event_response_t rsp = VMI_EVENT_RESPONSE_SET_REGISTERS;
    event->interrupt_event.reinject = 0;
    event->x86_regs->rip += event->interrupt_event.insn_length;

    vmi_pause_vm(vmi);
    shredder->stop = true;

    if ( !shredder->harness_bp && shredder->manual )
    {
        printf("Enter magic string location: \n");
        char word[256];
        char *w = fgets(word, sizeof(word), stdin);
        shredder->magic_string = strtoull(w, NULL, 0);
        shredder->harness_bp = true;
    }

    return rsp;
}

void mem_tracer_cb(mem_tracer_t *mem_tracer, page_access_log_t *page, addr_t va)
{
    shredder_t *shredder = mem_tracer->data;

    vmi_instance_t vmi = mem_tracer->vmi->libvmi;
    vmi_event_t *event = mem_tracer->event;

    if ( shredder->manual )
    {
        if ( shredder->magic_string != va )
            return;

        printf("Enter magic string: \n");
        char word[256], magic_string[8];
        char *w = fgets(word, sizeof(word), stdin);

        memcpy(&magic_string, w, 8);

        GHashTable *values = g_hash_table_new(g_int64_hash, g_int64_equal);
        g_hash_table_insert(values, &magic_string, GUINT_TO_POINTER(1));
        create_fork_and_fuzz(vmi_get_vmid(vmi), vmi_get_num_vcpus(vmi), shredder->map, values, event->x86_regs->cr3, event->x86_regs->rip, page->pa, va);
        g_hash_table_destroy(values);
        return;
    }

    create_fork_and_fuzz(vmi_get_vmid(vmi), vmi_get_num_vcpus(vmi), shredder->map, mem_tracer->values, event->x86_regs->cr3, event->x86_regs->rip, page->pa, va);
    return;
}

bool shredder(const char *domain, const char* json_path, bool fuzz, bool manual)
{
    shredder_t shredder = {0};
    vmi_t *vmi = NULL;
    vmi_instance_t libvmi = NULL;

    if ( !(shredder.map = g_try_malloc0(CODE_TRACER_MAP_SIZE)) )
        goto exit;

    if ( !(shredder.vmi = setup_vmi(domain, 0, json_path, true, true)) )
        goto exit;

    shredder.manual = manual;
    vmi = shredder.vmi;
    libvmi = vmi->libvmi;

    setup_signals();
    vmi_pause_vm(libvmi);

    printf("Waiting for harness int3\n");

    if ( !enable_callback(vmi, BREAKPOINT, int3_cb, &shredder) )
        goto exit;

    if ( loop_vmi(vmi, ~0, &shredder.stop) != STOP )
        goto exit;

    shredder.mem_tracer = mem_tracer_setup(vmi, fuzz ? mem_tracer_cb : NULL, &shredder);
    if ( !shredder.mem_tracer )
        goto exit;

    mem_tracer_trap_user_pages(shredder.mem_tracer, shredder.target_cr3);

    if ( loop_vmi(vmi, ~0, &shredder.stop) != STOP )
        goto exit;

    if ( !fuzz )
        mem_tracer_print_stats(shredder.mem_tracer);

    vmi_resume_vm(libvmi);

exit:
    if ( shredder.processes )
        g_hash_table_destroy(shredder.processes);
    if ( shredder.map )
        g_free(shredder.map);

    mem_tracer_close(shredder.mem_tracer, true);
    disable_callback(vmi, BREAKPOINT, int3_cb);
    close_vmi(vmi);
    return true;
}
