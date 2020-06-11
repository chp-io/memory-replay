#include "private.h"

static event_response_t mem_cb(vmi_instance_t libvmi, vmi_event_t *event)
{
    event_response_t rsp = 0;
    vmi_t *vmi = event->data;
    GSList *loop = vmi->event[MEMACCESS].cb;

    while (loop)
    {
        vmi_callback_t *cb = loop->data;
        event->data = cb->data;
        rsp |= cb->cb(libvmi, event);
        loop = loop->next;
    }

    if ( rsp & VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP )
    {
        /* If we already have singlestep on, don't disable it */
        if ( vmi->singlestep )
            rsp &= ~VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP;
        else
            vmi->singlestep = true;
    }

    event->data = vmi;
    return rsp;
}

static event_response_t int3_cb(vmi_instance_t libvmi, vmi_event_t *event)
{
    event_response_t rsp = 0;
    vmi_t *vmi = event->data;
    GSList *loop = vmi->event[BREAKPOINT].cb;

    while (loop)
    {
        vmi_callback_t *cb = loop->data;
        event->data = cb->data;
        rsp |= cb->cb(libvmi, event);
        loop = loop->next;
    }

    if ( rsp & VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP )
    {
        /* If we already have singlestep on, don't disable it */
        if ( vmi->singlestep )
            rsp &= ~VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP;
        else
            vmi->singlestep = true;
    }

    event->data = vmi;
    return rsp;
}

static event_response_t cr3_cb(vmi_instance_t libvmi, vmi_event_t *event)
{
    event_response_t rsp = 0;
    vmi_t *vmi = event->data;
    GSList *loop = vmi->event[CR3WRITE].cb;

    while (loop)
    {
        vmi_callback_t *scb = loop->data;
        event->data = scb->data;
        rsp |= scb->cb(libvmi, event);
        loop = loop->next;
    }

    event->data = vmi;
    return rsp;
}

static event_response_t singlestep_cb(vmi_instance_t libvmi, vmi_event_t *event)
{
    event_response_t rsp = 0;
    vmi_t *vmi = event->data;
    GSList *loop = vmi->event[SINGLESTEP].cb;

    while (loop)
    {
        vmi_callback_t *cb = loop->data;
        event->data = cb->data;
        rsp |= cb->cb(libvmi, event);
        loop = loop->next;
    }

    if ( rsp & VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP )
        vmi->singlestep = false;

    event->data = vmi;
    return rsp;
}

vmi_t *setup_vmi(const char* domain, uint64_t domid, const char* json_path, bool init_events, bool init_os)
{
    //fprintf(stderr, "Init vmi, events: %i domain %s domid %lu\n", init_events, domain, domid);
    vmi_t *vmi = g_try_malloc0(sizeof(vmi_t));
    if ( !vmi )
        return NULL;

    vmi_mode_t mode = (init_events ? VMI_INIT_EVENTS : 0) | (domain ? VMI_INIT_DOMAINNAME : VMI_INIT_DOMAINID);
    const void *d = domain?:(void*)&domid;

    if ( VMI_FAILURE == vmi_init(&vmi->libvmi, VMI_XEN, d, mode, NULL, NULL) )
        goto err;

    vmi_instance_t libvmi = vmi->libvmi;

    if ( init_os )
    {
        os = vmi_init_os(libvmi, VMI_CONFIG_JSON_PATH, (void*)json_path, NULL);

        if ( VMI_OS_UNKNOWN == os )
            goto err;
    }
    else
    {
        if ( VMI_PM_UNKNOWN == vmi_init_paging(libvmi, os == VMI_OS_WINDOWS ? VMI_PM_INITFLAG_TRANSITION_PAGES : 0) )
            goto err;
    }

    if ( init_events )
    {
        SETUP_MEM_EVENT(&vmi->event[MEMACCESS].handler, ~0ULL, VMI_MEMACCESS_RWX, mem_cb, true);
        SETUP_SINGLESTEP_EVENT(&vmi->event[SINGLESTEP].handler, ~0U, singlestep_cb, false);
        SETUP_REG_EVENT(&vmi->event[CR3WRITE].handler, CR3, VMI_REGACCESS_W, 0, cr3_cb);
        SETUP_INTERRUPT_EVENT(&vmi->event[BREAKPOINT].handler, int3_cb);

        vmi->event[MEMACCESS].handler.data = vmi;
        vmi->event[SINGLESTEP].handler.data = vmi;
        vmi->event[CR3WRITE].handler.data = vmi;
        vmi->event[BREAKPOINT].handler.data = vmi;
    }

    return vmi;

err:
    close_vmi(vmi);
    return NULL;
}

void close_vmi(vmi_t *vmi)
{
    if ( !vmi )
        return;

    int i;
    for ( i = 0; i < __EVENT_TYPE_MAX; i++)
    {
        GSList *cb = vmi->event[i].cb;
        if ( cb )
            g_slist_free_full(cb, (GDestroyNotify)g_free);
    }

    vmi_destroy(vmi->libvmi);
    g_free(vmi);
}

loop_exit_condition_t loop_vmi(vmi_t *vmi, unsigned int timeout, bool *stop)
{
    loop_exit_condition_t ret = ERROR;
    if ( !vmi )
        return ret;

    bool _s;
    bool *_stop = stop ?: &_s;
    *_stop = false;

    vmi_instance_t libvmi = vmi->libvmi;
    vmi_resume_vm(libvmi);

    while (!interrupted && timeout-- && !*_stop)
    {
        if ( vmi_events_listen(libvmi, 100) == VMI_FAILURE )
        {
            fprintf(stderr, "Error in vmi_events_listen!\n");
            break;
        }
    }

    if ( !stop )
        vmi_pause_vm(libvmi);
    vmi_events_listen(libvmi, 0);

    if ( interrupted < 0 )
        ret = SIGNAL;
    else if ( !timeout )
        ret = TIMEOUT;
    else if ( stop )
        ret = STOP;
    else
        ret = ERROR;

    interrupted = 0;

    return ret;
}

static bool toggle_event(vmi_t *vmi, enum event_types type)
{
    bool ret;
    if ( vmi->event[type].enabled )
        ret = VMI_SUCCESS == vmi_clear_event(vmi->libvmi, &vmi->event[type].handler, NULL);
    else
        ret = VMI_SUCCESS == vmi_register_event(vmi->libvmi, &vmi->event[type].handler);

    if ( ret )
        vmi->event[type].enabled = !vmi->event[type].enabled;

    return ret;
}

bool enable_callback(vmi_t *vmi, enum event_types type, event_callback_t cb, void *data)
{
    if ( !vmi )
        return false;

    vmi_callback_t *scb = g_try_malloc0(sizeof(vmi_callback_t));
    if ( !scb )
        return false;

    scb->cb = cb;
    scb->data = data;

    if ( !vmi->event[type].enabled && !toggle_event(vmi, type) )
    {
        g_free(scb);
        return false;
    }

    vmi->event[type].cb = g_slist_prepend(vmi->event[type].cb, scb);
    return true;
}

bool disable_callback(vmi_t *vmi, enum event_types type, event_callback_t cb)
{
    if ( !vmi )
        return false;

    if ( !vmi->event[type].enabled )
        return true;

    GSList *loop = vmi->event[type].cb;
    while (loop)
    {
        vmi_callback_t *scb = loop->data;
        if ( scb->cb == cb )
            break;
        loop = loop->next;
    }

    if ( loop )
        vmi->event[type].cb = g_slist_remove(vmi->event[type].cb, loop->data);

    if ( !vmi->event[type].cb )
        return toggle_event(vmi, type);

    return true;
}
