#include "udp.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>

#define CHECK_LAST_NULL(packet, offset) do { \
    if ((offset) > 0 && (packet)[(offset)-1] != '\0') (packet)[(offset)++] = '\0'; \
} while (0)

// Debug print macro (enable via -DDEBUG_PRINT)
static void debug(const char *fmt, ...) {
#ifdef DEBUG_PRINT
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
#endif
}

// Generate a new unique MessageID
uint16_t udp_next_message_id(UdpClient *client) {
    return client->message_id++;
}

// Initialize the UDP client (socket + settings)
int udp_client_init(UdpClient *client, const char *server_ip, uint16_t port,
                    uint16_t timeout_ms, uint8_t max_retries) {
    memset(client, 0, sizeof(UdpClient));

    client->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (client->sockfd < 0) {
        perror("socket");
        return -1;
    }

    memset(&client->server_addr, 0, sizeof(struct sockaddr_in));
    client->server_addr.sin_family = AF_INET;
    client->server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &client->server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        return -1;
    }

    client->addr_len = sizeof(struct sockaddr_in);
    client->message_id = 0;
    client->timeout_ms = timeout_ms;
    client->max_retries = max_retries;

    msgid_buffer_init(&client->seen_ids);
    return 0;
}

// Close the UDP socket
void udp_client_close(UdpClient *client) {
    if (client->sockfd > 0)
        close(client->sockfd);
}

// Send a CNFRM message in response to received message
int udp_send_confirm(UdpClient *client, uint16_t ref_msg_id, const struct sockaddr_in *target_addr) {
    uint8_t packet[3];
    packet[0] = MSG_CNFRM;
    uint16_t net_id = htons(ref_msg_id);
    memcpy(&packet[1], &net_id, sizeof(uint16_t));

    int sent = sendto(client->sockfd, packet, sizeof(packet), 0,
                      (struct sockaddr *)target_addr, client->addr_len);
    if (sent < 0) {
        perror("sendto (confirm)");
        return -1;
    }

    debug("Sending UDP Packet\n");
    #ifdef DEBUG_PRINT
    udp_print_packet(packet, sizeof(packet));
    #endif

    debug("[DEBUG] Sent CNFRM for ID %u\n", ref_msg_id);
    return 0;
}

int udp_send_message(UdpClient *client, packetContent_t *content) {
    if (!client || !content) return -1;

    uint8_t packet[MAX_MESSAGE_SIZE];
    size_t offset = 0;

    packet[offset++] = (uint8_t)content->type;

    uint16_t net_id = htons(content->messageID);
    memcpy(&packet[offset], &net_id, sizeof(uint16_t));
    offset += 2;

    switch (content->type) {
        case MSG_CNFRM: {
            // Overwrite the message ID with the reference ID
            uint16_t net_ref = htons(content->ref_messageID);
            memcpy(&packet[1], &net_ref, sizeof(uint16_t));
            offset = 3;
            break;
        }
        case MSG_REPLY: {
            packet[offset++] = content->result;
            uint16_t net_ref = htons(content->ref_messageID);
            memcpy(&packet[offset], &net_ref, sizeof(uint16_t));
            offset += 2;
            if (content->payload && content->length > 0) {
                memcpy(&packet[offset], content->payload, content->length);
                offset += content->length;
                CHECK_LAST_NULL(packet, offset);
            }
            break;
        }
        case MSG_AUTH: {
            size_t ulen = strlen(client->username) + 1;
            size_t dlen = strlen(client->display_name) + 1;
            memcpy(&packet[offset], client->username, ulen);
            offset += ulen;
            CHECK_LAST_NULL(packet, offset);
            memcpy(&packet[offset], client->display_name, dlen);
            offset += dlen;
            CHECK_LAST_NULL(packet, offset);
            if (content->payload && content->length > 0) {
                memcpy(&packet[offset], content->payload, content->length);
                offset += content->length;
                CHECK_LAST_NULL(packet, offset);
            }
            break;
        }
        case MSG_JOIN: {
            if (content->payload && content->length > 0) {
                memcpy(&packet[offset], content->payload, content->length);
                offset += content->length;
                CHECK_LAST_NULL(packet, offset);
                size_t dlen = strlen(client->display_name) + 1;
                memcpy(&packet[offset], client->display_name, dlen);
                offset += dlen;
                CHECK_LAST_NULL(packet, offset);
            }
            break;
        }
        case MSG_MSG:
        case MSG_ERR: {
            size_t dlen = strlen(client->display_name) + 1;
            memcpy(&packet[offset], client->display_name, dlen);
            offset += dlen;
            CHECK_LAST_NULL(packet, offset);
            if (content->payload && content->length > 0) {
                memcpy(&packet[offset], content->payload, content->length);
                offset += content->length;
                CHECK_LAST_NULL(packet, offset);
            }
            break;
        }
        case MSG_BYE: {
            size_t dlen = strlen(client->display_name) + 1;
            memcpy(&packet[offset], client->display_name, dlen);
            offset += dlen;
            CHECK_LAST_NULL(packet, offset);
            break;
        }
        case MSG_PING:
            break;
        default:
            fprintf(stderr, "udp_send_message: Unknown message type\n");
            return -1;
    }

    if (offset > MAX_MESSAGE_SIZE) return -1;

    debug("Sending UDP Packet\n");
    #ifdef DEBUG_PRINT
    udp_print_packet(packet, offset);
    #endif

    ssize_t sent = sendto(client->sockfd, packet, offset, 0,
                          (struct sockaddr *)&client->server_addr, client->addr_len);
    if (sent < 0) {
        perror("sendto");
        return -1;
    }

    return 0;
}

int udp_receive_message(UdpClient *client, uint8_t *buffer, size_t buffer_size,
                        UdpMessageType *out_type, uint16_t *out_message_id,
                        struct sockaddr_in *source_addr) {
    struct pollfd pfd = { .fd = client->sockfd, .events = POLLIN };

    int ready = poll(&pfd, 1, client->timeout_ms);

    if (ready < 0) {
        perror("poll");
        return -1;
    } else if (ready == 0) {
        debug("[DEBUG] Timeout waiting for message\n");
        return 0;
    }

    socklen_t addr_len = sizeof(struct sockaddr_in);
    ssize_t received = recvfrom(client->sockfd, buffer, buffer_size, 0,
                                (struct sockaddr *)source_addr, &addr_len);
    if (received < 3) {
        debug("[DEBUG] Received too short message: %ld bytes\n", received);
        return -2;
    }

    debug("Incoming UDP Packet\n");
    #ifdef DEBUG_PRINT
    udp_print_packet(buffer, received);
    #endif
    

    *out_type = buffer[0];
    uint16_t net_id;
    memcpy(&net_id, &buffer[1], sizeof(uint16_t));
    *out_message_id = ntohs(net_id);

    return received;
}

int udp_send_with_confirm(UdpClient *client, packetContent_t *packet) {
    if (!client || !packet) return -1;

    uint16_t msg_id = udp_next_message_id(client);
    packet->messageID = msg_id;
    debug("[DEBUG], WORKING WITH ID %u\n", msg_id);

    for (int attempt = 0; attempt <= client->max_retries; ++attempt) {
        if (udp_send_message(client, packet) != 0)
            return -1;

        for (int noBanPls = 5; noBanPls>0; noBanPls--) {
            uint8_t buf[MAX_MESSAGE_SIZE];
            UdpMessageType recv_type;
            uint16_t recv_id;
            struct sockaddr_in source;

            int ret = udp_receive_message(client, buf, sizeof(buf), &recv_type, &recv_id, &source);
            if (ret == 0) break;
            if (ret < 0) continue;

            if (recv_type == MSG_CNFRM && recv_id == msg_id) {
                return 0;
            }
        
            // NOVÁ část – kontrola REPLY
            if (recv_type == MSG_REPLY && ret >= 6) {
                uint8_t result = buf[3];
                uint16_t ref_id;
                memcpy(&ref_id, &buf[4], 2);
                ref_id = ntohs(ref_id);
        
                printf("ref_id ?= msg_id: %u ?= %u\n", ref_id, msg_id);
                if (ref_id == msg_id) {
                    // Optionally parse the message content: &buf[6]
                    return (result == 1) ? 0 : -2;  // success/fail na základě REPLY
                }
            }

            if (!msgid_buffer_contains(&client->seen_ids, recv_id)) {
                msgid_buffer_add(&client->seen_ids, recv_id);
                udp_send_confirm(client, recv_id, &source);
            }
        }
    }

    fprintf(stderr, "ERROR: Confirm not received after %d attempts.\n", client->max_retries);
    return -1;
}

bool parse_auth_payload(UdpClient *client, const char *args, packetContent_t *out_packet) {
    if (!client || !args || !out_packet) return false;

    static char payload[128];  // static kvůli životnosti mimo funkci
    size_t offset = 0;

    const char *username = args;
    const char *next = strchr(username, ' ');
    if (!next) return false;
    size_t len = next - username;
    if (len == 0 || len >= sizeof(client->username)) return false;
    strncpy(client->username, username, len);
    client->username[len] = '\0';

    const char *secret = next + 1;
    next = strchr(secret, ' ');
    if (!next) return false;
    len = next - secret;
    if (len == 0 || len >= 64) return false;
    memcpy(&payload[offset], secret, len);
    offset += len;
    payload[offset++] = '\0';

    const char *display_name = next + 1;
    len = strlen(display_name);
    if (len == 0 || len >= sizeof(client->display_name)) return false;
    strncpy(client->display_name, display_name, sizeof(client->display_name) - 1);
    client->display_name[sizeof(client->display_name) - 1] = '\0';

    out_packet->type = MSG_AUTH;
    out_packet->payload = (uint8_t *)payload;
    out_packet->length = offset;
    return true;
}

// Main client loop
int udp_run(const client_config_t *cfg) {
    UdpClient client;
    client_state_t state = STATE_INIT;

    if (udp_client_init(&client, cfg->server, cfg->port,
                        cfg->udp_confirm_timeout_ms, cfg->udp_max_retries) != 0)
        return 1;

    strncpy(client.display_name, "anonymous", sizeof(client.display_name) - 1);
    strncpy(client.username, "anonymous", sizeof(client.username) - 1);

    printf("Connected as %s. Type /help for commands.\n", client.display_name);

    struct pollfd pfds[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = client.sockfd, .events = POLLIN }
    };

    while (1) {
        if (poll(pfds, 2, -1) < 0) {
            perror("poll");
            break;
        }

        if (pfds[0].revents & POLLIN) {
            char line[512];
            if (!fgets(line, sizeof(line), stdin)) break;
            line[strcspn(line, "\n")] = 0;

            if (strncmp(line, "/auth ", 6) == 0) {
                packetContent_t pkt;
                if (parse_auth_payload(&client, &line[6], &pkt)) {
                    if (udp_send_with_confirm(&client, &pkt) == 0) {
                        state = STATE_AUTHORIZED;
                        printf("Authorized as %s.\n", client.display_name);
                    } else {
                        fprintf(stderr, "Authorization failed.\n");
                    }
                } else {
                    fprintf(stderr, "Usage: /auth <username> <secret> <display_name>\n");
                }
            } else if (strcmp(line, "/help") == 0) {
                printf("Commands:\n");
                printf("  /auth <username> <secret> <display_name>\n");
                if (state == STATE_AUTHORIZED) {
                    printf("  /join <channel>\n  /rename <name>\n  /quit\n");
                }
            } else if (strcmp(line, "/quit") == 0) {
                packetContent_t pkt = { .type = MSG_BYE, .payload = NULL, .length = 0 };
                udp_send_with_confirm(&client, &pkt);
                break;
            } else if (state != STATE_AUTHORIZED) {
                fprintf(stderr, "Please authenticate first using /auth.\n");
            } else if (strncmp(line, "/join ", 6) == 0) {
                packetContent_t pkt = {
                    .type = MSG_JOIN,
                    .payload = (uint8_t *)&line[6],
                    .length = strlen(&line[6]) + 1
                };
                udp_send_with_confirm(&client, &pkt);
            } else if (strncmp(line, "/rename ", 8) == 0) {
                strncpy(client.display_name, &line[8], sizeof(client.display_name) - 1);
                client.display_name[sizeof(client.display_name) - 1] = 0;
                printf("Display name set to: %s\n", client.display_name);
            } else {
                packetContent_t pkt = {
                    .type = MSG_MSG,
                    .payload = (uint8_t *)line,
                    .length = strlen(line) + 1
                };
                udp_send_with_confirm(&client, &pkt);
            }
        }

        if (pfds[1].revents & POLLIN) {
            uint8_t buf[MAX_MESSAGE_SIZE];
            UdpMessageType type;
            uint16_t msg_id;
            struct sockaddr_in src;

            int ret = udp_receive_message(&client, buf, sizeof(buf), &type, &msg_id, &src);
            if (ret <= 0) continue;

            if (msgid_buffer_contains(&client.seen_ids, msg_id)) {
                continue;
            }

            msgid_buffer_add(&client.seen_ids, msg_id);
            if (type != MSG_CNFRM) {
                udp_send_confirm(&client, msg_id, &src);
            }

            if (type == MSG_MSG || type == MSG_REPLY) {
                printf("%s\n", &buf[3]);
            } else if (type == MSG_ERR || type == MSG_BYE) {
                printf("Server ended session.\n");
                break;
            }
        }
    }

    udp_client_close(&client);
    return 0;
}
