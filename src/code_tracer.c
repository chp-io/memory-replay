#include "private.h"

static uint8_t bp = 0xCC;

static void track_coverage(code_tracer_t *code_tracer, uint64_t cur_loc, uint64_t va)
{
    cur_loc = (cur_loc >> 4) ^ (cur_loc << 8);
    cur_loc &= CODE_TRACER_MAP_SIZE - 1;
    addr_t coverage_point = cur_loc ^ code_tracer->prev_loc;

    code_tracer->map[coverage_point]++;
    code_tracer->prev_loc = cur_loc >> 1;

    if ( code_tracer->map[coverage_point] == 1 )
    {
        printf("\t\tNew code hit at 0x%lx (prev_loc: 0x%lx)\n", va, code_tracer->prev_loc);

        code_tracer->points++;
        code_tracer->counter = 0; // reset cf counter to allow exploration of the new path

        if ( code_tracer->cb )
            code_tracer->cb(code_tracer);
    }
}

static bool is_cf(unsigned int id)
{
    switch ( id )
    {
        case X86_INS_JA:
        case X86_INS_JAE:
        case X86_INS_JBE:
        case X86_INS_JB:
        case X86_INS_JCXZ:
        case X86_INS_JECXZ:
        case X86_INS_JE:
        case X86_INS_JGE:
        case X86_INS_JG:
        case X86_INS_JLE:
        case X86_INS_JL:
        case X86_INS_JMP:
        case X86_INS_LJMP:
        case X86_INS_JNE:
        case X86_INS_JNO:
        case X86_INS_JNP:
        case X86_INS_JNS:
        case X86_INS_JO:
        case X86_INS_JP:
        case X86_INS_JRCXZ:
        case X86_INS_JS:
        case X86_INS_CALL:
        case X86_INS_RET:
        case X86_INS_RETF:
        case X86_INS_RETFQ:
        case X86_INS_INT3:
            return true;
        default:
            break;
    }

    return false;
}

static addr_t next_cf_insn(vmi_instance_t vmi, addr_t cr3, addr_t start, bool *int3)
{
    cs_insn *insn;

    size_t count;
    size_t read;

    unsigned char buff[CODE_TRACER_BUFFER_SIZE] = { 0 };
    addr_t next_cf = 0;

    access_context_t ctx = {
        .translate_mechanism = VMI_TM_PROCESS_DTB,
        .dtb = cr3,
        .addr = start
    };

    if ( VMI_FAILURE == vmi_read(vmi, &ctx, CODE_TRACER_BUFFER_SIZE, buff, &read) )
    {
        //printf("Failed to grab memory from 0x%lx\n", start);
        return 0;
    }

    count = cs_disasm(cs_handle, buff, read, start, 0, &insn);
    if ( count ) {
        size_t j;
        for ( j=0; j<count; j++) {
             //printf("Next instruction @ 0x%lx: %s!\n", insn[j].address, insn[j].mnemonic);

             if ( is_cf(insn[j].id) )
             {
                next_cf = insn[j].address;

                *int3 = (insn[j].id == X86_INS_INT3);

                //printf("\tFound next control flow instruction @ 0x%lx: %s!\n", next_cf, insn[j].mnemonic);
                break;
             }
        }
        cs_free(insn, count);
    }

    return next_cf;
}

static event_response_t singlestep_cb(vmi_instance_t vmi, vmi_event_t *event)
{
    addr_t pa = (event->ss_event.gfn << 12) + event->ss_event.offset;
    code_tracer_t *code_tracer = (code_tracer_t *)event->data;

    track_coverage(code_tracer, pa, event->x86_regs->rip);

    access_context_t ctx = {
        .translate_mechanism = VMI_TM_PROCESS_DTB,
        .dtb = event->x86_regs->cr3,
    };

    bool int3;
    code_tracer->next_cf = next_cf_insn(vmi, event->x86_regs->cr3, event->x86_regs->rip, &int3);
    vmi_pagetable_lookup(vmi, event->x86_regs->cr3, code_tracer->next_cf, &code_tracer->next_cf_pa);

    if ( int3 || !code_tracer->next_cf || code_tracer->next_cf == event->interrupt_event.gla )
    {
        vmi_pause_vm(vmi);
        *code_tracer->stop = true;
        return VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP;
    }

    ctx.addr = code_tracer->next_cf;
    vmi_read_8(vmi, &ctx, &code_tracer->backup);
    vmi_write_8(vmi, &ctx, &bp);

    return VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP;
}

static event_response_t int3_cb(vmi_instance_t vmi, vmi_event_t *event)
{
    addr_t pa = (event->interrupt_event.gfn << 12) + event->interrupt_event.offset;
    code_tracer_t *code_tracer = (code_tracer_t *)event->data;

    code_tracer->counter++;
    track_coverage(code_tracer, pa, event->x86_regs->rip);

    event->interrupt_event.reinject = 0;

    if ( event->interrupt_event.gla != code_tracer->next_cf )
    {
        event->interrupt_event.reinject = 1;
        vmi_pause_vm(vmi);
        *code_tracer->stop = true;
        return 0;
    }

    if ( code_tracer->counter >= CODE_TRACER_CF_LIMIT )
    {
        printf("[CODE_TRACER] Stopping after exceeding CF limit\n");
        vmi_pause_vm(vmi);
        *code_tracer->stop = true;
        return 0;
    }

    access_context_t ctx = {
        .translate_mechanism = VMI_TM_PROCESS_DTB,
        .dtb = event->x86_regs->cr3,
    };

    ctx.addr = event->interrupt_event.gla;
    vmi_write_8(vmi, &ctx, &code_tracer->backup);

    return VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP;
}

code_tracer_t *code_tracer_setup(vmi_t *vmi, uint8_t *map, bool *stop)
{
    code_tracer_t *code_tracer = g_try_malloc0(sizeof(code_tracer_t));
    if ( !code_tracer )
        return NULL;

    code_tracer->vmi = vmi;
    code_tracer->map = map;
    code_tracer->stop = stop;

    if ( !enable_callback(vmi, BREAKPOINT, int3_cb, code_tracer) )
        goto err;
    if ( !enable_callback(vmi, SINGLESTEP, singlestep_cb, code_tracer) )
        goto err;

    return code_tracer;

err:
    g_free(code_tracer);
    disable_callback(vmi, BREAKPOINT, int3_cb);
    disable_callback(vmi, SINGLESTEP, singlestep_cb);
    return NULL;
}

bool code_tracer_start(code_tracer_t *code_tracer, addr_t pagetable, addr_t start)
{
    vmi_instance_t vmi = code_tracer->vmi->libvmi;
    bool int3 = false;

    code_tracer->prev_loc = 0;
    code_tracer->counter = 0;

    if ( !(code_tracer->next_cf = next_cf_insn(vmi, pagetable, start, &int3)) || int3 )
    {
        printf("[CODE_TRACER] Skipping memory location because no more CF left\n");
        return false;
    }

    if ( VMI_FAILURE == vmi_pagetable_lookup(vmi, pagetable, code_tracer->next_cf, &code_tracer->next_cf_pa) )
        return false;
    if ( VMI_FAILURE == vmi_read_8_pa(vmi, code_tracer->next_cf_pa, &code_tracer->backup) )
        return false;
    if ( VMI_FAILURE == vmi_write_8_pa(vmi, code_tracer->next_cf_pa, &bp) )
        return false;

    return true;
}

void code_tracer_close(code_tracer_t *code_tracer)
{
    if ( !code_tracer )
        return;

    disable_callback(code_tracer->vmi, BREAKPOINT, int3_cb);
    disable_callback(code_tracer->vmi, SINGLESTEP, singlestep_cb);

    if ( code_tracer->next_cf_pa )
        vmi_write_8_pa(code_tracer->vmi->libvmi, code_tracer->next_cf_pa, &code_tracer->backup);

    g_free(code_tracer);
}
