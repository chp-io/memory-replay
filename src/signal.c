#include "private.h"

static struct sigaction act;

static void close_handler(int sig)
{
    printf("Interrupted: %i\n", -sig);
    interrupted = -sig;
}

void setup_signals(void)
{
    act.sa_handler = close_handler;
    sigemptyset(&act.sa_mask);
    sigaction(SIGHUP,  &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT,  &act, NULL);
    sigaction(SIGALRM, &act, NULL);
}
