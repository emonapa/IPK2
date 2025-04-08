#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include "tcp.h"

// Simple client states
typedef enum {
    CLIENT_CLOSED,
    CLIENT_AUTH,
    CLIENT_OPEN,
    CLIENT_END
} client_state_t;

// Holds the TCP client's runtime info
static struct {
    int sock;
    client_state_t state;
    char displayName[32];
    char username[32];
    char secret[128];

    // If set, we are waiting for a REPLY or ERR before next command
    int waitingForReply;
    // Buffer for partial lines
    char lineBuf[8192];
    int  lineLen;
} g_tcp;



bool tcp_parse_line(const char *line, tcp_message_t *msg)
{
    memset(msg, 0, sizeof(*msg));
    msg->type = TCP_MSG_UNKNOWN;

    // Check known patterns:
    if (strncmp(line, "ERR FROM ", 9) == 0) {
        msg->type = TCP_MSG_ERR;
        // "ERR FROM <name> IS <content>"
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
    }
    else if (strncmp(line, "BYE FROM ", 9) == 0) {
        msg->type = TCP_MSG_BYE;
        const char *p = line + 9;
        if (strlen(p) >= sizeof(msg->displayName)) return false;
        strcpy(msg->displayName, p);
        return true;
    }
    else if (strncmp(line, "REPLY ", 6) == 0) {
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
    }
    else if (strncmp(line, "MSG FROM ", 9) == 0) {
        msg->type = TCP_MSG_MSG;
        // "MSG FROM <name> IS <content>"
        const char *p = line + 9;
        const char *isPtr = strstr(p, " IS ");
        if (!isPtr) return false;
        size_t dlen = isPtr - p;
        if (dlen >= sizeof(msg->displayName)) dlen = sizeof(msg->displayName)-1;
        memcpy(msg->displayName, p, dlen);
        const char *cPtr = isPtr + 4;
        if (strlen(cPtr) >= sizeof(msg->content)) return false;
        strcpy(msg->content, cPtr);
        return true;
    }
    else if (strncmp(line, "AUTH ", 5) == 0) {
        msg->type = TCP_MSG_AUTH;
        return true;
    }
    else if (strncmp(line, "JOIN ", 5) == 0) {
        msg->type = TCP_MSG_JOIN;
        return true;
    }
    return false;
}



// Process one fully received line from the server
static void process_server_line(const char *line)
{
    tcp_message_t msg;
    memset(&msg, 0, sizeof(msg));
    if (!tcp_parse_line(line, &msg)) {
        fprintf(stderr, "Protocol error. Received malformed line: %s\n", line);
        // According to spec, we should send ERR and end
        char errBuf[256];
        snprintf(errBuf, sizeof(errBuf), "ERR FROM %s IS Protocol parse error\r\n", g_tcp.displayName);
        send(g_tcp.sock, errBuf, strlen(errBuf), 0);
        g_tcp.state = CLIENT_END;
        return;
    }

    switch(msg.type) {
    case TCP_MSG_ERR:
        fprintf(stdout, "ERROR FROM %s: %s\n", msg.displayName, msg.content);
        g_tcp.state = CLIENT_END;
        g_tcp.waitingForReply = 0;
        break;
    case TCP_MSG_BYE:
        fprintf(stderr, "Received BYE from %s\n", msg.displayName);
        g_tcp.state = CLIENT_END;
        g_tcp.waitingForReply = 0;
        break;
    case TCP_MSG_REPLY:
        if (msg.replyOk) {
            fprintf(stdout, "Action Success: %s\n", msg.content);
            fprintf(stdout, "State: %d\n", g_tcp.state);
            if (g_tcp.state == CLIENT_CLOSED || g_tcp.state == CLIENT_AUTH)
                g_tcp.state = CLIENT_OPEN;
        } else {
            fprintf(stdout, "Action Failure: %s\n", msg.content);
        }
        g_tcp.waitingForReply = 0;
        break;
    case TCP_MSG_MSG:
        fprintf(stdout, "%s: %s\n", msg.displayName, msg.content);
        break;
    default:
        // Possibly AUTH, JOIN from server. Rare or unused. Just ignore
        break;
    }
}

// Send a text line, but only if not waiting for previous result
static void send_line(const char *line)
{
    if (g_tcp.waitingForReply) {
        fprintf(stderr, "ERROR: still waiting for previous request to complete.\n");
        return;
    }
    send(g_tcp.sock, line, strlen(line), 0);
    // If it's a request that might produce a REPLY, set waiting
    // For simplicity, assume /auth, /join => waiting
    if (strncmp(line, "AUTH ", 5) == 0 ||
        strncmp(line, "JOIN ", 5) == 0) {
        g_tcp.waitingForReply = 1;
    }
}

// Process local command from user
static void process_local_command(const char *cmdLine)
{
    char copyBuf[512];
    strncpy(copyBuf, cmdLine, sizeof(copyBuf)-1);
    copyBuf[sizeof(copyBuf)-1] = 0;

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
        return;
    }
    else if (strcmp(tokens[0], "/auth") == 0) {
        if (count < 4) {
            fprintf(stderr, "Usage: /auth user secret displayName\n");
            return;
        }
        strcpy(g_tcp.username, tokens[1]);
        strcpy(g_tcp.secret, tokens[2]);
        strcpy(g_tcp.displayName, tokens[3]);
        char line[512];
        snprintf(line, sizeof(line), "AUTH %s AS %s USING %s\r\n",
                 g_tcp.username, g_tcp.displayName, g_tcp.secret);
        send_line(line);
        g_tcp.state = CLIENT_AUTH;
    }
    else if (strcmp(tokens[0], "/join") == 0) {
        if (count < 2) {
            fprintf(stderr, "Usage: /join channel\n");
            return;
        }
        if (g_tcp.state != CLIENT_OPEN) {
            fprintf(stderr, "ERROR: not in OPEN state.\n");
            return;
        }
        char line[512];
        snprintf(line, sizeof(line), "JOIN %s AS %s\r\n",
                 tokens[1], g_tcp.displayName);
        send_line(line);
    }
    else if (strcmp(tokens[0], "/rename") == 0) {
        if (count < 2) {
            fprintf(stderr, "Usage: /rename newName\n");
            return;
        }
        strcpy(g_tcp.displayName, tokens[1]);
        fprintf(stdout, "Renamed locally to: %s\n", g_tcp.displayName);
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", tokens[0]);
    }
}

// Implementation of the main TCP run
int tcp_run(const client_config_t *cfg)
{
    g_tcp.sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_tcp.sock < 0) {
        perror("socket");
        return 1;
    }
    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(cfg->port);
    if (inet_pton(AF_INET, cfg->server, &srv.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server address.\n");
        close(g_tcp.sock);
        return 1;
    }
    if (connect(g_tcp.sock, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        perror("connect");
        close(g_tcp.sock);
        return 1;
    }

    fprintf(stderr, "TCP connected to %s:%d\n", cfg->server, cfg->port);
    g_tcp.state = CLIENT_CLOSED;
    strcpy(g_tcp.displayName, "UserTCP");
    g_tcp.waitingForReply = 0;
    g_tcp.lineLen = 0;

    struct pollfd fds[2];
    fds[0].fd = g_tcp.sock;
    fds[0].events = POLLIN;
    fds[1].fd = STDIN_FILENO;
    fds[1].events = POLLIN;


    int max_tries = 3;
    while (g_tcp.state != CLIENT_END && max_tries > 0) {
        //max_tries--;
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        // socket
        if (fds[0].revents & POLLIN) {
            int n = recv(g_tcp.sock, g_tcp.lineBuf + g_tcp.lineLen,
                         sizeof(g_tcp.lineBuf) - g_tcp.lineLen - 1, 0);
            if (n <= 0) {
                fprintf(stderr, "Server closed or error.\n");
                break;
            }
            g_tcp.lineLen += n;
            g_tcp.lineBuf[g_tcp.lineLen] = 0;
            // parse lines up to \r\n
            char *start = g_tcp.lineBuf;
            while (1) {
                char *crlf = strstr(start, "\r\n");
                if (!crlf) break;
                *crlf = 0;
                process_server_line(start);
                crlf += 2;
                start = crlf;
            }
            // shift leftover
            int leftover = g_tcp.lineBuf + g_tcp.lineLen - start;
            memmove(g_tcp.lineBuf, start, leftover);
            g_tcp.lineLen = leftover;
        }
        // stdin
        if (fds[1].revents & POLLIN) {
            char inputBuf[1024];
            if (!fgets(inputBuf, sizeof(inputBuf), stdin)) {
                fprintf(stderr, "EOF on stdin.\n");
                break;
            }
            int l = strlen(inputBuf);
            if (l>0 && inputBuf[l-1]=='\n') inputBuf[l-1] = 0;

            if (inputBuf[0] == '/') {
                process_local_command(inputBuf);
            } else {
                if (g_tcp.state != CLIENT_OPEN) {
                    fprintf(stderr, "ERROR: not in OPEN state.\n");
                } else if (g_tcp.waitingForReply) {
                    fprintf(stderr, "ERROR: waiting for previous request.\n");
                } else {
                    char sendbuf[2048];
                    snprintf(sendbuf, sizeof(sendbuf),
                             "MSG FROM %s IS %s\r\n",
                             g_tcp.displayName, inputBuf);
                    send(g_tcp.sock, sendbuf, strlen(sendbuf), 0);
                }
            }
        }
    }

    // send BYE if not ended
    if (g_tcp.state != CLIENT_END) {
        char byeLine[128];
        snprintf(byeLine, sizeof(byeLine), "BYE FROM %s\r\n", g_tcp.displayName);
        send(g_tcp.sock, byeLine, strlen(byeLine), 0);
    }
    close(g_tcp.sock);
    return 0;
}
