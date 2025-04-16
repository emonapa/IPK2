#include <stdio.h>
#include <string.h>
#include <time.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "client.h"

// Start a timer
struct timespec start_timer() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
}

// Return elapsed milliseconds
long get_elapsed_ms(struct timespec start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now); 
    return (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
}

int resolve_server_address(const char *host, uint16_t port, struct sockaddr_in *out_addr) {
    struct addrinfo hints, *res;
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_DGRAM;

    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }

    if (res == NULL) {
        fprintf(stderr, "No address info found for %s\n", host);
        return -1;
    }

    struct sockaddr_in *addr_in = (struct sockaddr_in *)res->ai_addr;
    memcpy(out_addr, addr_in, sizeof(struct sockaddr_in));
    freeaddrinfo(res);
    return 0;
}
