#ifndef UDP_H
#define UDP_H

#include <stdint.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include "utils.h"   // for msgid_buffer_t
#include "client.h"  // for client_config_t

#define MAX_MESSAGE_SIZE 65507  // Maximum safe UDP payload size
#define MAX_RETRIES 3
#define DEFAULT_TIMEOUT_MS 250

// IPK25-CHAT UDP message types
typedef enum {
    MSG_CNFRM = 0x00,  // Confirmation
    MSG_REPLY = 0x01,  // Server reply to action
    MSG_AUTH  = 0x02,  // Authorization request
    MSG_JOIN  = 0x03,  // Join a channel
    MSG_MSG   = 0x04,  // Regular chat message
    MSG_PING  = 0xFD,  // Ping
    MSG_ERR   = 0xFE,  // Error message
    MSG_BYE   = 0xFF   // Disconnect
} UdpMessageType;

// UDP client state structure
typedef struct {
    int sockfd;
    struct sockaddr_in server_addr;     // Initial server address (for AUTH)
    struct sockaddr_in dyn_server_addr; // Dynamic address after AUTH
    socklen_t addr_len;

    uint16_t message_id;           // Counter for unique MessageIDs
    msgid_buffer_t seen_ids;       // Buffer to track received MessageIDs

    uint16_t timeout_ms;           // Receive timeout
    uint8_t max_retries;           // Max send attempts
    char display_name[64];         // Display name of the user
    char username[64];             // Username
} UdpClient;

// Client state (initial / authorized)
typedef enum {
    STATE_INIT,
    STATE_AUTHORIZED
} client_state_t_udp;


// Representation of an outgoing message
typedef struct {
    UdpMessageType type;
    uint16_t messageID;
    uint16_t ref_messageID;
    uint8_t result;
    uint8_t *payload;
    size_t length;
} packetContent_t;

// --- Initialization and shutdown ---
int udp_client_init(UdpClient *client, const char *server_ip, uint16_t port,
                    uint16_t timeout_ms, uint8_t max_retries);
void udp_client_close(UdpClient *client);

// --- Message ID generator ---
uint16_t udp_next_message_id(UdpClient *client);

// --- Sending messages ---
int udp_send_message(UdpClient *client, packetContent_t *content);
int udp_send_confirm(UdpClient *client, uint16_t ref_msg_id);

// Sends a message and waits for CNFRM (no REPLY expected)
// 'buf' must be a buffer of at least MAX_MESSAGE_SIZE bytes
int udp_send_with_confirm(UdpClient *client, packetContent_t *packet);

// Sends a message and waits for CNFRM and then REPLY
// On REPLY, the client's dynamic address is updated
int udp_send_with_reply(UdpClient *client, packetContent_t *packet,
                        uint8_t *buf, size_t buf_len);

// --- Receiving messages ---
int udp_receive_message(UdpClient *client, uint8_t *buffer, size_t buffer_size,
                        struct sockaddr_in *source_addr);

// --- Client main loop ---
int udp_run(const client_config_t *cfg);

#endif // UDP_H
