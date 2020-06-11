#ifndef PRIVATE_H
#define PRIVATE_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/wait.h>

#define LIBVMI_EXTRA_GLIB
#define LIBVMI_EXTRA_JSON
#include <libvmi/libvmi.h>
#include <libvmi/events.h>
#include <libvmi/libvmi_extra.h>
#include <libvmi/x86.h>
#include <libvmi/slat.h>

#include <glib.h>

#define XC_WANT_COMPAT_EVTCHN_API 1
#define XC_WANT_COMPAT_MAP_FOREIGN_API 1
#include <xenctrl.h>
#define LIBXL_API_VERSION 0x041300
#include <libxl.h>

#include <capstone.h>

#include "signal.h"
#include "vmi.h"
#include "shredder.h"
#include "fuzz.h"
#include "mem_tracer.h"
#include "code_tracer.h"
#include "forkvm.h"

extern os_t os;
extern xc_interface *xc;
extern csh cs_handle;
extern int interrupted;
extern bool manual, refresh;

gint gint64_compare(gconstpointer ptr_a, gconstpointer ptr_b);

#endif
