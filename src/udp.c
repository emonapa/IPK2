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
#include <signal.h>

#define CHECK_LAST_NULL(packet, offset) do { \
    if ((offset) > 0 && (packet)[(offset)-1] != '\0') (packet)[(offset)++] = '\0'; \
} while (0)

static void debug(const char *fmt, ...) {
#ifdef DEBUG_PRINT
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
#endif
}

// Globální flag pro ukončení programu
volatile sig_atomic_t should_exit = 0;

// Signal handler pro Ctrl+C
void handle_sigint(int signum) {
    (void)signum;
    should_exit = 1;
}

uint16_t udp_next_message_id(UdpClient *client) {
    return client->message_id++;
}

int udp_client_init(UdpClient *client, const char *server_host, uint16_t port,
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
    if (resolve_server_address(server_host, port, &client->server_addr) != 0) {
        fprintf(stderr, "Failed to resolve server address\n");
        return -1;
    }

    memcpy(&client->dyn_server_addr, &client->server_addr, sizeof(struct sockaddr_in));
    client->addr_len = sizeof(struct sockaddr_in);
    client->message_id = 0;
    client->timeout_ms = timeout_ms;
    client->max_retries = max_retries;

    msgid_buffer_init(&client->seen_ids);
    return 0;
}

#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Zpracuje a vypíše obsah ERR zprávy ze serveru
void handle_error_message(const uint8_t *buf, size_t length) {
    if (!buf || length < 4) {
        fprintf(stderr, "ERROR: Invalid ERR packet (too short)\n");
        return;
    }

    const char *display_name = (const char *)&buf[3];
    size_t name_len = strnlen(display_name, length - 3);

    if (3 + name_len + 1 >= length) {
        fprintf(stderr, "ERROR: Malformed ERR packet (no message)\n");
        return;
    }

    const char *message = display_name + name_len + 1;

    fprintf(stdout, "ERROR FROM %s: %s\n", display_name, message);
}


void udp_client_close(UdpClient *client) {
    if (client->sockfd > 0)
        close(client->sockfd);
}

int udp_send_confirm(UdpClient *client, uint16_t ref_msg_id) {
    uint8_t packet[3];
    packet[0] = MSG_CNFRM;
    uint16_t net_id = htons(ref_msg_id);
    memcpy(&packet[1], &net_id, sizeof(uint16_t));

    debug("Sending\n");
    #ifdef DEBUG_PRINT
    udp_print_packet(packet, sizeof(packet));
    #endif
    debug("[DEBUG] Sending CNFRM for ID %u to %s:%u\n",
          ref_msg_id, inet_ntoa(client->dyn_server_addr.sin_addr),
          ntohs(client->dyn_server_addr.sin_port));
    int sent = sendto(client->sockfd, packet, sizeof(packet), 0,
                      (struct sockaddr *)&client->dyn_server_addr, client->addr_len);
    if (sent < 0) {
        perror("sendto (confirm)");
        return -1;
    }

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
        case MSG_CNFRM:
            memcpy(&packet[1], &net_id, sizeof(uint16_t));
            offset = 3;
            break;
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

    debug("Sending\n");
    #ifdef DEBUG_PRINT
    udp_print_packet(packet, offset);
    #endif
    debug("[DEBUG] Sending %zu bytes to %s:%u\n",
          offset, inet_ntoa(client->dyn_server_addr.sin_addr),
          ntohs(client->dyn_server_addr.sin_port));
    ssize_t sent = sendto(client->sockfd, packet, offset, 0,
                          (struct sockaddr *)&client->dyn_server_addr, client->addr_len);
    if (sent < 0) {
        perror("sendto");
        return -1;
    }

    return 0;
}

int udp_receive_message(UdpClient *client, uint8_t *buffer, size_t buffer_size,
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
    int ret = recvfrom(client->sockfd, buffer, buffer_size, 0,
        (struct sockaddr *)source_addr, &addr_len);

    debug("Recevied\n");
    #ifdef DEBUG_PRINT
    udp_print_packet(buffer, ret);
    #endif

    return ret;
}

int udp_send_with_confirm(UdpClient *client, packetContent_t *packet,
                          uint8_t *buf, size_t buf_len) {
    if (!client || !packet || !buf) return -1;
    uint16_t msg_id = udp_next_message_id(client);
    packet->messageID = msg_id;

    for (int attempt = 0; attempt <= client->max_retries; ++attempt) {
        if (udp_send_message(client, packet) != 0)
            return -1;

        while (1) {
            struct sockaddr_in source;
            int ret = udp_receive_message(client, buf, buf_len, &source);
            if (ret == 0) break;
            if (ret < 3) continue;

            uint8_t type = buf[0];
            uint16_t id;
            memcpy(&id, &buf[1], sizeof(uint16_t));
            id = ntohs(id);

            if (type == MSG_ERR && id == msg_id) {
                handle_error_message(buf, ret);
                break;
            }

            if (type == MSG_CNFRM && id == msg_id)
                return 0;
        }
    }

    fprintf(stderr, "ERROR: Confirm not received after %d attempts.\n", client->max_retries);
    return -1;
}

int udp_send_with_reply(UdpClient *client, packetContent_t *packet,
                        uint8_t *buf, size_t buf_len) {
    if (!client || !packet || !buf) return -1;

    uint16_t msg_id = udp_next_message_id(client);
    packet->messageID = msg_id;
    int confirmed = 0;
    // Just for the warning
    confirmed = confirmed + 0;

    for (int attempt = 0; attempt <= client->max_retries; ++attempt) {
        if (udp_send_message(client, packet) != 0)
            return -1;

        while (1) {
            struct sockaddr_in source;
            int ret = udp_receive_message(client, buf, buf_len, &source);
            if (ret == 0) break;
            if (ret < 3) continue;

            uint8_t type = buf[0];
            uint16_t id;
            memcpy(&id, &buf[1], sizeof(uint16_t));
            id = ntohs(id);

            if (type == MSG_ERR && id == msg_id) {
                handle_error_message(buf, ret);
                break;
            }

            if (type == MSG_CNFRM && id == msg_id) {
                confirmed = 1;
                continue;
            }

            if (type == MSG_REPLY && ret >= 6) {
                uint16_t ref_id;
                memcpy(&ref_id, &buf[4], sizeof(uint16_t));
                ref_id = ntohs(ref_id);
                if (ref_id == msg_id) {
                    memcpy(&client->dyn_server_addr, &source, sizeof(struct sockaddr_in));

                    if (!msgid_buffer_contains(&client->seen_ids, id)) {
                        msgid_buffer_add(&client->seen_ids, id);
                        udp_send_confirm(client, id);
                    }

                    debug("[DEBUG] Updated server port to %u based on REPLY\n", ntohs(source.sin_port));
                    return 0;
                }
            }

            if (type != MSG_CNFRM && !msgid_buffer_contains(&client->seen_ids, id)) {
                msgid_buffer_add(&client->seen_ids, id);
                udp_send_confirm(client, id);
            }
        }
    }

    fprintf(stderr, "ERROR: REPLY not received after %d attempts.\n", client->max_retries);
    return -1;
}

bool parse_auth_payload(UdpClient *client, const char *args, packetContent_t *out_packet) {
    if (!client || !args || !out_packet) return false;

    static char payload[128];
    size_t offset = 0;

    const char *username = args;
    const char *next = strchr(username, ' ');
    if (!next) return false;
    size_t len = next - username;
    if (len >= sizeof(client->username)) return false;
    strncpy(client->username, username, len);
    client->username[len] = '\0';

    const char *secret = next + 1;
    next = strchr(secret, ' ');
    if (!next) return false;
    len = next - secret;
    if (len >= 64) return false;
    memcpy(&payload[offset], secret, len);
    offset += len;
    payload[offset++] = '\0';

    const char *display_name = next + 1;
    len = strlen(display_name);
    if (len >= sizeof(client->display_name)) return false;
    strncpy(client->display_name, display_name, sizeof(client->display_name) - 1);
    client->display_name[sizeof(client->display_name) - 1] = '\0';

    out_packet->type = MSG_AUTH;
    out_packet->payload = (uint8_t *)payload;
    out_packet->length = offset;
    return true;
}

int udp_run(const client_config_t *cfg) {
    UdpClient client;
    client_state_t state = STATE_INIT;
    signal(SIGINT, handle_sigint);

    if (udp_client_init(&client, cfg->server, cfg->port,
                        cfg->udp_confirm_timeout_ms, cfg->udp_max_retries) != 0)
        return 1;

    strncpy(client.display_name, "anonymous", sizeof(client.display_name) - 1);
    strncpy(client.username, "anonymous", sizeof(client.username) - 1);

    uint8_t buffer[MAX_MESSAGE_SIZE];

    printf("Connected as %s. Type /help for commands.\n", client.display_name);

    struct pollfd pfds[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = client.sockfd, .events = POLLIN }
    };

    while (1) {
        if (should_exit) {
            packetContent_t pkt = { .type = MSG_BYE, .payload = NULL, .length = 0 };
            udp_send_with_confirm(&client, &pkt, buffer, sizeof(buffer));
            break;
        }

        if (poll(pfds, 2, -1) < 0) {
            if (errno == EINTR) continue; // přerušeno signálem
            perror("poll");
            break;
        }

        if (pfds[0].revents & POLLIN) {
            char line[512];
            if (!fgets(line, sizeof(line), stdin)) {
                should_exit = 1;
                continue;
            }
            line[strcspn(line, "\n")] = 0;
        
            if (strncmp(line, "/auth ", 6) == 0) {
                packetContent_t pkt;
                if (parse_auth_payload(&client, &line[6], &pkt)) {
                    if (udp_send_with_reply(&client, &pkt, buffer, sizeof(buffer)) == 0) {
                        uint8_t result = buffer[3];
                        const char *msg = (const char *)&buffer[6];
                        printf(result ? "Action Success: %s\n" : "Action Failure: %s\n", msg);
                        if (result) {
                            state = STATE_AUTHORIZED;
                            printf("Authorized as %s.\n", client.display_name);
                        }
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
                udp_send_with_confirm(&client, &pkt, buffer, sizeof(buffer));
                break;
            } else if (state != STATE_AUTHORIZED) {
                fprintf(stderr, "Please authenticate first using /auth.\n");
            } else if (strncmp(line, "/join ", 6) == 0) {
                packetContent_t pkt = {
                    .type = MSG_JOIN,
                    .payload = (uint8_t *)&line[6],
                    .length = strlen(&line[6]) + 1
                };
                if (udp_send_with_reply(&client, &pkt, buffer, sizeof(buffer)) == 0) {
                    uint8_t result = buffer[3];
                    const char *msg = (const char *)&buffer[6];
                    printf(result ? "Action Success: %s\n" : "Action Failure: %s\n", msg);
                } else {
                    fprintf(stderr, "Join failed.\n");
                }
            } else if (strncmp(line, "/rename ", 8) == 0) {
                strncpy(client.display_name, &line[8], sizeof(client.display_name) - 1);
                client.display_name[sizeof(client.display_name) - 1] = 0;
                debug("Display name set to: %s\n", client.display_name);
            } else {
                packetContent_t pkt = {
                    .type = MSG_MSG,
                    .payload = (uint8_t *)line,
                    .length = strlen(line) + 1
                };
                udp_send_with_confirm(&client, &pkt, buffer, sizeof(buffer));
            }
        }
        

        if (pfds[1].revents & POLLIN) {
            struct sockaddr_in src;
            int ret = udp_receive_message(&client, buffer, sizeof(buffer), &src);
            if (ret <= 0) continue;

            uint8_t type = buffer[0];
            uint16_t msg_id;
            memcpy(&msg_id, &buffer[1], sizeof(uint16_t));
            msg_id = ntohs(msg_id);

            if (msgid_buffer_contains(&client.seen_ids, msg_id)) continue;

            msgid_buffer_add(&client.seen_ids, msg_id);
            if (type != MSG_CNFRM) 
                udp_send_confirm(&client, msg_id);
            
            if (type == MSG_REPLY) {
                uint8_t result = buffer[3];
                const char *msg = (const char *)&buffer[6];
                printf(result ? "Action Success: %s\n" : "Action Failure: %s\n", msg);
            } else if (type == MSG_MSG) {
                const char *display = (const char *)&buffer[3];
                const char *message = (const char *)&buffer[3 + strlen(display) + 1];
                printf("[%s]: %s\n", display, message);
            } else if (type == MSG_ERR) {
                handle_error_message(buffer, ret);
                break;
            }
        }
    }

    udp_client_close(&client);
    return 0;
}
