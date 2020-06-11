#ifndef VMI_H
#define VMI_H

typedef enum event_types {
    MEMACCESS,
    SINGLESTEP,
    BREAKPOINT,
    CR3WRITE,
    __EVENT_TYPE_MAX
} event_types_t;

typedef struct {
    void *data;
    event_callback_t cb;
} vmi_callback_t;

typedef struct {
    vmi_instance_t libvmi;

    struct {
        bool enabled;
        vmi_event_t handler;
        GSList *cb; // list of vmi_callback_t
    } event[__EVENT_TYPE_MAX];

    bool singlestep;
    int interrupted;
    int error;
} vmi_t;

vmi_t *setup_vmi(const char* domain, uint64_t domid, const char* json_path, bool init_events, bool init_os);
void close_vmi(vmi_t *vmi);

typedef enum loop_exit_condition {
    SIGNAL,
    ERROR,
    TIMEOUT,
    STOP
} loop_exit_condition_t;

static const char *exit_cond_strings[] = {
    [SIGNAL] = "Signal",
    [ERROR] = "Error",
    [TIMEOUT] = "Timeout",
    [STOP] = "Stop"
};

static inline const char *exit_cond_str(loop_exit_condition_t c) {
    return exit_cond_strings[c];
}

loop_exit_condition_t loop_vmi(vmi_t *vmi, unsigned int timeout, bool *stop);

bool enable_callback(vmi_t *vmi, event_types_t type, event_callback_t cb, void *data);
bool disable_callback(vmi_t *vmi, event_types_t type, event_callback_t cb);

#endif
