#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "client.h"

#define TIMEOUT 250
#define MAX_RETRIES 3
#define PORT 4567

static void print_help(void) {
    printf("Usage: ipk25chat-client -t [tcp|udp] -s server -p port -d timeout -r retries\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    client_config_t config;
    // Default values
    memset(&config, 0, sizeof(config));
    config.port = PORT;       // default
    config.timeout_ms = TIMEOUT; // default
    config.max_retries = MAX_RETRIES; // default

    // PARSE ARGS
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            print_help();
        } else if (strcmp(argv[i], "-t") == 0 && i+1 < argc) {
            strncpy(config.transport, argv[++i], sizeof(config.transport));
        } else if (strcmp(argv[i], "-s") == 0 && i+1 < argc) {
            strncpy(config.server, argv[++i], sizeof(config.server));
        } else if (strcmp(argv[i], "-p") == 0 && i+1 < argc) {
            config.port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i+1 < argc) {
            config.timeout_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0 && i+1 < argc) {
            config.max_retries = atoi(argv[++i]);
        }
    }

    if (strlen(config.transport) == 0 || strlen(config.server) == 0) {
        fprintf(stderr, "Missing required arguments!\n");
        return 1;
    }

    // RUN CLIENT
    int ret = 0;
    if (strcmp(config.transport, "tcp") == 0) {
        ret = run_client_tcp(&config);
    } else {
        ret = run_client_udp(&config);
    }

    return ret;
}
