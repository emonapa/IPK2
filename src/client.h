#ifndef CLIENT_H
#define CLIENT_H

// Configuration structure for the entire client
typedef struct {
    char transport[8];               // "tcp" or "udp"
    char server[256];                // Server address or hostname
    int  port;                       // Server port
    int  udp_confirm_timeout_ms;     // UDP confirmation timeout (ms)
    int  udp_max_retries;            // UDP max retransmissions
} client_config_t;

// The unified entry point that decides whether to run TCP or UDP
int client_run(client_config_t *cfg);

#endif // CLIENT_H
