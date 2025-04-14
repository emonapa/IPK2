#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MSGID_BUFFER_SIZE 1024

typedef struct {
    uint16_t ids[MSGID_BUFFER_SIZE];
    int start;
    int count;
} msgid_buffer_t;

// Initialize the buffer
void msgid_buffer_init(msgid_buffer_t *buf);

// Check for duplicate
bool msgid_buffer_contains(const msgid_buffer_t *buf, uint16_t id);

// Add ID to buffer (overwrite oldest if full)
void msgid_buffer_add(msgid_buffer_t *buf, uint16_t id);

// Print the contents of a UDP packet
void udp_print_packet(const uint8_t *buf, size_t len);

#endif // UTILS_H
