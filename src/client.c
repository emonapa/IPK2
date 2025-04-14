#include <stdio.h>
#include <string.h>
#include "tcp.h"     // We include .c files to keep everything in 6 sources
#include "udp.h"     // so we can call tcp_run(), udp_run() here
#include "client.h"


// The main client_run function decides which variant to start
int client_run(client_config_t *cfg) {
    if (strcmp(cfg->transport, "tcp") == 0) {
        return tcp_run(cfg);
    } else if (strcmp(cfg->transport, "udp") == 0) {
        udp_run(cfg);
        return 0;
    } else {
        fprintf(stderr, "Unsupported transport: %s\n", cfg->transport);
        return 1;
    }
}
