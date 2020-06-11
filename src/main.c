#include <stdio.h>

#include "private.h"

os_t os;
bool manual, refresh;

int interrupted;
xc_interface *xc;
csh cs_handle;
int vcpus;

static inline void usage(void)
{
    fprintf(stderr,
            "-d/--domain            VM name\n"
            "-j/--json-path         Path to VM's kernel json config file\n"
            "-f/--fuzz              Turn on memory fuzzing (off by default)\n"
            "-m/--manual            Perform manual memory fuzz\n"
    );
}

int main(int argc, char** argv)
{
    int ret = -1, c, long_index = 0;
    const struct option long_opts[] =
    {
        {"domain", required_argument, NULL, 'd'},
        {"json-path", required_argument, NULL, 'j'},
        {"fuzz", optional_argument, NULL, 'f'},
        {"manual", optional_argument, NULL, 'm'},
        {NULL, 0, NULL, 0}
    };
    const char* opts = "d:j:fm";

    const char* domain = NULL, *json_path = NULL;
    bool fuzz = false;
    manual = false;

    while ((c = getopt_long (argc, argv, opts, long_opts, &long_index)) != -1)
    {
        switch(c)
        {
        case 'd':
            domain = optarg;
            break;
        case 'j':
            json_path = optarg;
            break;
        case 'f':
            fuzz = true;
            break;
        case 'm':
            manual = true;
            break;
        default:
            break;
        };
    }

    if ( !domain || !json_path )
    {
        usage();
        return ret;
    }

    if ( !(xc = xc_interface_open(0,0,0)) )
        goto done;

    if ( cs_open(CS_ARCH_X86, CS_MODE_64, &cs_handle) )
        goto done;

    shredder(domain, json_path, fuzz, manual);
    ret = 0;

done:
    cs_close(&cs_handle);
    xc_interface_close(xc);

    return ret;
}
