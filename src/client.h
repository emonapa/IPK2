#ifndef CLIENT_H
#define CLIENT_H

#include <stdint.h>
#include <netinet/in.h>

// Configuration structure for the entire client
typedef struct {
    char transport[8];               // "tcp" or "udp"
    char server[256];                // Server address or hostname
    int  port;                       // Server port
    int  udp_confirm_timeout_ms;     // UDP confirmation timeout (ms)
    int  udp_max_retries;            // UDP max retransmissions
} client_config_t;

struct timespec start_timer();
long get_elapsed_ms(struct timespec start);
int resolve_server_address(const char *host, uint16_t port, struct sockaddr_in *out_addr);

#endif // CLIENT_H
