#ifndef UDP_H
#define UDP_H

#include <stdint.h>
#include <netinet/in.h>
#include <stdbool.h>
#include "utils.h"  // for msgid_buffer_t
#include "client.h" // for client_config_t

#define MAX_MESSAGE_SIZE 65507  // Maximum safe UDP payload size
#define MAX_RETRIES 3
#define DEFAULT_TIMEOUT_MS 250

// Message types for the IPK25-CHAT protocol (UDP)
typedef enum {
    MSG_CNFRM = 0x00,  // Confirmation message
    MSG_REPLY = 0x01,
    MSG_AUTH  = 0x02,
    MSG_JOIN  = 0x03,
    MSG_MSG   = 0x04,
    MSG_PING  = 0xFD,
    MSG_ERR   = 0xFE,
    MSG_BYE   = 0xFF
} UdpMessageType;

// UDP client state
typedef struct {
    int sockfd;
    struct sockaddr_in server_addr;
    struct sockaddr_in dyn_server_addr;
    socklen_t addr_len;

    uint16_t message_id;           // Counter for unique MessageIDs
    msgid_buffer_t seen_ids;       // Circular buffer for received MessageIDs

    uint16_t timeout_ms;
    uint8_t max_retries;
    char display_name[64];         // Display name of client (used in /rename)
    char username[64]; 
} UdpClient;

typedef enum {
    STATE_INIT,
    STATE_AUTHORIZED
} client_state_t;

// UDP client state
typedef struct {
    UdpMessageType type;
    uint16_t messageID;
    uint16_t ref_messageID;
    uint8_t result;
    uint8_t *payload;
    size_t length;
} packetContent_t;

// Initialization and cleanup
int udp_client_init(UdpClient *client, const char *server_ip, uint16_t port,
                    uint16_t timeout_ms, uint8_t max_retries);
void udp_client_close(UdpClient *client);

// Communication and message handling
int udp_send_message(UdpClient *client, packetContent_t *content);

int udp_receive_message(UdpClient *client, uint8_t *buffer, size_t buffer_size,
    UdpMessageType *out_type, uint16_t *out_message_id);

int udp_send_confirm(UdpClient *client, uint16_t ref_msg_id);

int udp_send_with_confirm(UdpClient *client, packetContent_t *packet);

// Message ID generator
uint16_t udp_next_message_id(UdpClient *client);

// Main UDP client loop
int udp_run(const client_config_t *cfg);

#endif // UDP_H
