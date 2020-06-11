bin_PROGRAMS = shredder

shredder_SOURCES = src/main.c src/private.h \
                   src/signal.c src/signal.h \
                   src/vmi.c src/vmi.h \
                   src/shredder.c src/shredder.h \
                   src/mem_tracer.c src/mem_tracer.h \
                   src/code_tracer.c src/code_tracer.h \
                   src/forkvm.c src/forkvm.h \
                   src/fuzz.c src/fuzz.h

shredder_LDADD   = $(LIBS) \
                   $(VMI_LIBS) \
                   $(CAPSTONE_LIBS) \
                   $(GLIB_LIBS)
shredder_CFLAGS  = -Wall -Wextra -fstack-protector \
                   $(CFLAGS) \
                   $(VMI_CFLAGS) \
                   $(CAPSTONE_CFLAGS) \
                   $(GLIB_CFLAGS)
