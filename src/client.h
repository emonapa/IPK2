#ifndef CLIENT_H
#define CLIENT_H

typedef struct {
    char transport[8];
    char server[256];
    int port;
    int timeout_ms;   // (UDP) milisekundy
    int max_retries;  // (UDP) kolikrat zkusim znovu
} client_config_t;

// Stavová struktura klienta
typedef enum {
    ST_CLOSED,
    ST_AUTH,
    ST_OPEN,
    ST_END
} client_state_e;

// Protokol upřesněn jinde, ale můžeme držet i tady
// např. definice stránek, do kterých si pamatuji username, displayname atd.

int run_client_tcp(const client_config_t *cfg);
int run_client_udp(const client_config_t *cfg);

#endif // CLIENT_H
