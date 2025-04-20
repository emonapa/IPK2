#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>

#include "tcp.h"
#include "utils.h"
#include "client.h"

// Debug print function, only enabled when DEBUG_PRINT is defined
static void debug(const char *fmt, ...) {
#ifdef DEBUG_PRINT
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
#endif
}

// Global flag for graceful TCP shutdown on SIGINT
volatile sig_atomic_t terminate_tcp = 0;

// Signal handler for SIGINT
void handle_sigint_tcp(int signo) {
    (void)signo;
    terminate_tcp = 1;
}

// Parses a line received from the server and fills a tcp_message_t struct
bool tcp_parse_line(const char *line, tcp_message_t *msg)
{
    memset(msg, 0, sizeof(*msg));
    msg->type = TCP_MSG_UNKNOWN;

    if (strncmp(line, "ERR FROM ", 9) == 0) {
        msg->type = TCP_MSG_ERR;
        const char *p = line + 9;
        const char *isPtr = strstr(p, " IS ");
        if (!isPtr) return false;
        size_t dlen = isPtr - p;
        if (dlen >= sizeof(msg->displayName)) dlen = sizeof(msg->displayName) - 1;
        memcpy(msg->displayName, p, dlen);
        const char *cPtr = isPtr + 4;
        if (strlen(cPtr) >= sizeof(msg->content)) return false;
        strcpy(msg->content, cPtr);
        return true;
    } else if (strncmp(line, "BYE FROM ", 9) == 0) {
        msg->type = TCP_MSG_BYE;
        const char *p = line + 9;
        if (strlen(p) >= sizeof(msg->displayName)) return false;
        strcpy(msg->displayName, p);
        return true;
    } else if (strncmp(line, "REPLY ", 6) == 0) {
        msg->type = TCP_MSG_REPLY;
        const char *p = line + 6;
        if (strncmp(p, "OK IS ", 6) == 0) {
            msg->replyOk = 1;
            const char *cPtr = p + 6;
            if (strlen(cPtr) >= sizeof(msg->content)) return false;
            strcpy(msg->content, cPtr);
            return true;
        } else if (strncmp(p, "NOK IS ", 7) == 0) {
            msg->replyOk = 0;
            const char *cPtr = p + 7;
            if (strlen(cPtr) >= sizeof(msg->content)) return false;
            strcpy(msg->content, cPtr);
            return true;
        } else {
            return false;
        }
    } else if (strncmp(line, "MSG FROM ", 9) == 0) {
        msg->type = TCP_MSG_MSG;
        const char *p = line + 9;
        const char *isPtr = strstr(p, " IS ");
        if (!isPtr) return false;
        size_t dlen = isPtr - p;
        if (dlen >= sizeof(msg->displayName)) dlen = sizeof(msg->displayName) - 1;
        memcpy(msg->displayName, p, dlen);
        const char *cPtr = isPtr + 4;
        if (strlen(cPtr) >= sizeof(msg->content)) return false;
        strcpy(msg->content, cPtr);
        return true;
    } else if (strncmp(line, "AUTH ", 5) == 0) {
        msg->type = TCP_MSG_AUTH;
        return true;
    } else if (strncmp(line, "JOIN ", 5) == 0) {
        msg->type = TCP_MSG_JOIN;
        return true;
    }

    return false;
}

// Handles a parsed message from the server and updates client state accordingly
static void process_server_line(tcp_client_t *client, const char *line)
{
    tcp_message_t msg;
    memset(&msg, 0, sizeof(msg));
    if (!tcp_parse_line(line, &msg)) {
        fprintf(stderr, "Protocol error. Received malformed line: %s\n", line);
        char errBuf[256];
        snprintf(errBuf, sizeof(errBuf), "ERR FROM %s IS Protocol parse error\r\n", client->displayName);
        send(client->sock, errBuf, strlen(errBuf), 0);
        client->state = CLIENT_END;
        return;
    }

    switch (msg.type) {
        case TCP_MSG_ERR:
            fprintf(stdout, "ERROR FROM %s: %s\n", msg.displayName, msg.content);
            client->state = CLIENT_END;
            client->waitingForReply = 0;
            break;
        case TCP_MSG_BYE:
            fprintf(stderr, "Received BYE from %s\n", msg.displayName);
            client->state = CLIENT_END;
            client->waitingForReply = 0;
            break;
        case TCP_MSG_REPLY:
            if (msg.replyOk) {
                fprintf(stdout, "Action Success: %s\n", msg.content);
                if (client->state == CLIENT_CLOSED || client->state == CLIENT_AUTH)
                    client->state = CLIENT_OPEN;
            } else {
                fprintf(stdout, "Action Failure: %s\n", msg.content);
            }
            client->waitingForReply = 0;
            break;
        case TCP_MSG_MSG:
            fprintf(stdout, "%s: %s\n", msg.displayName, msg.content);
            break;
        default:
            break;
    }
}

// Sends a line to the server and optionally sets waitingForReply flag
static void send_line(tcp_client_t *client, const char *line)
{
    if (client->waitingForReply) {
        fprintf(stderr, "ERROR: still waiting for previous request to complete.\n");
        return;
    }
    send(client->sock, line, strlen(line), 0);
    if (strncmp(line, "AUTH ", 5) == 0 ||
        strncmp(line, "JOIN ", 5) == 0) {
        client->waitingForReply = 1;
    }
}

// Handles user input that begins with '/'
static void process_local_command(tcp_client_t *client, const char *cmdLine)
{
    char copyBuf[512];
    strncpy(copyBuf, cmdLine, sizeof(copyBuf) - 1);
    copyBuf[sizeof(copyBuf) - 1] = 0;

    char *tokens[10];
    int count = 0;
    char *p = strtok(copyBuf, " ");
    while (p && count < 10) {
        tokens[count++] = p;
        p = strtok(NULL, " ");
    }
    if (count == 0) return;

    if (strcmp(tokens[0], "/help") == 0) {
        fprintf(stdout, "Commands:\n");
        fprintf(stdout, "  /auth <user> <secret> <display>\n");
        fprintf(stdout, "  /join <channel>\n");
        fprintf(stdout, "  /rename <newDisplayName>\n");
        fprintf(stdout, "  /help\n");
    } else if (strcmp(tokens[0], "/auth") == 0) {
        if (count < 4) {
            fprintf(stdout, "ERROR: Usage: /auth user secret displayName\n");
            return;
        }
        strcpy(client->username, tokens[1]);
        strcpy(client->secret, tokens[2]);
        strcpy(client->displayName, tokens[3]);
        char line[512];
        snprintf(line, sizeof(line), "AUTH %s AS %s USING %s\r\n",
                 client->username, client->displayName, client->secret);
        send_line(client, line);
        client->state = CLIENT_AUTH;
    } else if (strcmp(tokens[0], "/join") == 0) {
        if (count < 2) {
            fprintf(stdout, "ERROR: Usage: /join channel\n");
            return;
        }
        if (client->state != CLIENT_OPEN) {
            fprintf(stdout, "ERROR: not in OPEN state.\n");
            return;
        }
        char line[512];
        snprintf(line, sizeof(line), "JOIN %s AS %s\r\n",
                 tokens[1], client->displayName);
        send_line(client, line);
    } else if (strcmp(tokens[0], "/rename") == 0) {
        if (count < 2) {
            fprintf(stdout, "ERROR: Usage: /rename newName\n");
            return;
        }
        strcpy(client->displayName, tokens[1]);
        debug("Renamed locally to: %s\n", client->displayName);
    } else {
        fprintf(stdout, "ERROR: Unknown command: %s\n", tokens[0]);
    }
}

// Main TCP client routine that connects to the server, handles user input and server responses.
int tcp_run(const client_config_t *cfg)
{
    tcp_client_t client;

    client.sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client.sock < 0) {
        perror("socket");
        return 1;
    }

    signal(SIGINT, handle_sigint_tcp);

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(cfg->port);

    if (resolve_server_address(cfg->server, cfg->port, &srv) != 0) {
        fprintf(stderr, "Failed to resolve server address: %s\n", cfg->server);
        close(client.sock);
        return 1;
    }

    if (connect(client.sock, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        perror("connect");
        close(client.sock);
        return 1;
    }

    debug("TCP connected to %s:%d\n", cfg->server, cfg->port);

    client.state = CLIENT_CLOSED;
    strcpy(client.displayName, "UserTCP");
    client.waitingForReply = 0;
    client.lineLen = 0;

    struct pollfd fds[2];
    fds[0].fd = client.sock;
    fds[0].events = POLLIN;
    fds[1].fd = STDIN_FILENO;
    fds[1].events = POLLIN;

    while (client.state != CLIENT_END) {
        if (terminate_tcp) {
            fprintf(stderr, "Received SIGINT. Exiting...\n");
            break;
        }

        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        // Handle incoming data from server
        if (fds[0].revents & POLLIN) {
            int n = recv(client.sock, client.lineBuf + client.lineLen,
                         sizeof(client.lineBuf) - client.lineLen - 1, 0);
            if (n <= 0) {
                debug("Server closed or error.\n");
                break;
            }
            client.lineLen += n;
            client.lineBuf[client.lineLen] = 0;

            char *start = client.lineBuf;
            while (1) {
                char *crlf = strstr(start, "\r\n");
                if (!crlf) break;
                *crlf = 0;
                process_server_line(&client, start);
                crlf += 2;
                start = crlf;
            }

            int leftover = client.lineBuf + client.lineLen - start;
            memmove(client.lineBuf, start, leftover);
            client.lineLen = leftover;
        }

        // Handle user input from stdin
        if (fds[1].revents & POLLIN) {
            char inputBuf[1024];
            if (!fgets(inputBuf, sizeof(inputBuf), stdin)) {
                debug("EOF on stdin.\n");
                break;
            }
            int l = strlen(inputBuf);
            if (l > 0 && inputBuf[l - 1] == '\n') inputBuf[l - 1] = 0;

            if (inputBuf[0] == '/') {
                process_local_command(&client, inputBuf);
            } else {
                if (client.state != CLIENT_OPEN) {
                    fprintf(stdout, "ERROR: not in OPEN state.\n");
                } else if (client.waitingForReply) {
                    fprintf(stdout, "ERROR: waiting for previous request.\n");
                } else {
                    char sendbuf[2048];
                    snprintf(sendbuf, sizeof(sendbuf),
                             "MSG FROM %s IS %s\r\n",
                             client.displayName, inputBuf);
                    send(client.sock, sendbuf, strlen(sendbuf), 0);
                }
            }
        }
    }

    // Send BYE message before closing connection
    if (client.state != CLIENT_END) {
        char byeLine[128];
        snprintf(byeLine, sizeof(byeLine), "BYE FROM %s\r\n", client.displayName);
        send(client.sock, byeLine, strlen(byeLine), 0);
    }

    close(client.sock);
    return 0;
}
