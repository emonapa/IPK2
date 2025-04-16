#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "client.h"
#include "tcp.h"
#include "udp.h"

#define DEFAULT_PORT 4567
#define DEFAULT_UDP_TIMEOUT 250 // ms
#define DEFAULT_UDP_RETRIES 3

void print_usage()
{
    fprintf(stderr, "Usage: ipk25chat-client [OPTIONS]\n");
    fprintf(stderr, "  -t <tcp|udp>        Transport protocol (required)\n");
    fprintf(stderr, "  -s <server>         Server IP or hostname (required)\n");
    fprintf(stderr, "  -p <port>           Server port (default: 4567)\n");
    fprintf(stderr, "  -d <timeout_ms>     UDP confirmation timeout in ms (default: 250)\n");
    fprintf(stderr, "  -r <retries>        UDP max retries (default: 3)\n");
    fprintf(stderr, "  -h                  Print this help\n");
}

int main(int argc, char *argv[])
{
    // Default config
    client_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.port = DEFAULT_PORT;
    cfg.udp_confirm_timeout_ms = DEFAULT_UDP_TIMEOUT;
    cfg.udp_max_retries = DEFAULT_UDP_RETRIES;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-t") == 0 && (i+1 < argc)) {
            strncpy(cfg.transport, argv[++i], sizeof(cfg.transport)-1);
        } else if (strcmp(argv[i], "-s") == 0 && (i+1 < argc)) {
            strncpy(cfg.server, argv[++i], sizeof(cfg.server)-1);
        } else if (strcmp(argv[i], "-p") == 0 && (i+1 < argc)) {
            cfg.port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && (i+1 < argc)) {
            cfg.udp_confirm_timeout_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0 && (i+1 < argc)) {
            cfg.udp_max_retries = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    // Check mandatory args
    if (strlen(cfg.transport) == 0 || strlen(cfg.server) == 0) {
        fprintf(stderr, "Error: -t and -s are required.\n");
        return 1;
    }

    if (strcmp(cfg.transport, "tcp") == 0) {
        return tcp_run(&cfg);
    } else if (strcmp(cfg.transport, "udp") == 0) {
        return udp_run(&cfg);
    } else {
        fprintf(stderr, "Unsupported transport: %s\n", cfg.transport);
        return 1;
    }
}
