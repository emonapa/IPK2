#ifndef TCP_H
#define TCP_H

#include "client.h"
#include <stdbool.h>

// Message type enum for TCP parsing
typedef enum {
    TCP_MSG_AUTH,
    TCP_MSG_JOIN,
    TCP_MSG_MSG,
    TCP_MSG_ERR,
    TCP_MSG_BYE,
    TCP_MSG_REPLY,
    TCP_MSG_UNKNOWN
} tcp_msg_type_e;

// Structure for a parsed TCP message
typedef struct {
    tcp_msg_type_e type;
    char displayName[32];
    char content[60000];
    int replyOk;  // 1 if REPLY OK, 0 if REPLY NOK
} tcp_message_t;

// Simple client states
typedef enum {
    CLIENT_CLOSED,
    CLIENT_AUTH,
    CLIENT_OPEN,
    CLIENT_END
} client_state_t_tcp;

// Holds the TCP client's runtime info
typedef struct {
    int sock;
    client_state_t_tcp state;
    char displayName[32];
    char username[32];
    char secret[128];

    // If set, we are waiting for a REPLY or ERR before next command
    int waitingForReply;
    // Buffer for partial lines
    char lineBuf[8192];
    int  lineLen;
} tcp_client_t;

// Parses a single line from the server. Returns true if successful
bool tcp_parse_line(const char *line, tcp_message_t *msg);

// Runs the TCP variant of the client
int tcp_run(const client_config_t *cfg);

#endif // TCP_H
