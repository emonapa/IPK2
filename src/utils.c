#include "utils.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>


void msgid_buffer_init(msgid_buffer_t *buf) {
    buf->start = 0;
    buf->count = 0;
}

bool msgid_buffer_contains(const msgid_buffer_t *buf, uint16_t id) {
    for (int i = 0; i < buf->count; i++) {
        int index = (buf->start + i) % MSGID_BUFFER_SIZE;
        if (buf->ids[index] == id)
            return true;
    }
    return false;
}

void msgid_buffer_add(msgid_buffer_t *buf, uint16_t id) {
    int index = (buf->start + buf->count) % MSGID_BUFFER_SIZE;
    buf->ids[index] = id;

    if (buf->count < MSGID_BUFFER_SIZE) {
        buf->count++;
    } else {
        buf->start = (buf->start + 1) % MSGID_BUFFER_SIZE; // overwrite oldest
    }
}



void udp_print_packet(const uint8_t *buf, size_t len) {
    if (!buf || len < 3) {
        printf("Invalid packet (too short)\n");
        return;
    }
    printf("=========================\n");
    printf("RAW message: ");
    for (size_t i = 0; i < len; i++) printf("%02X ", buf[i]);
    printf("\n");

    uint8_t type = buf[0];
    uint16_t msg_id;
    memcpy(&msg_id, &buf[1], 2);
    msg_id = ntohs(msg_id);

    switch (type) {
        case 0x00: printf("(CONFIRM)\n"); break;
        case 0x01: printf("(REPLY)\n"); break;
        case 0x02: printf("(AUTH)\n"); break;
        case 0x03: printf("(JOIN)\n"); break;
        case 0x04: printf("(MSG)\n"); break;
        case 0xFD: printf("(PING)\n"); break;
        case 0xFE: printf("(ERR)\n"); break;
        case 0xFF: printf("(BYE)\n"); break;
        default:   printf("(UNKNOWN)\n"); break;
    }

    if (type == 0x00) 
        printf("Ref MessageID: %u\n", msg_id);
    else
        printf("MessageID: %u\n", msg_id);

    if (type == 0x00 || type == 0xFD) {
        // CONFIRM or PING – no payload
        printf("=========================\n\n");
        return;
    }

    // Start of payload
    const uint8_t *payload = &buf[3];
    size_t payload_len = len - 3;

    switch (type) {
        case 0x01: { // REPLY
            if (payload_len < 3) {
                printf("Malformed REPLY packet\n");
                return;
            }
            uint8_t result = payload[0];
            uint16_t ref_id;
            memcpy(&ref_id, &payload[1], 2);
            ref_id = ntohs(ref_id);

            printf("  Result: %s\n", result ? "OK" : "NOK");
            printf("  Ref_MessageID: %u\n", ref_id);
            if (payload_len > 3)
                printf("  MessageContents: %s\n", (char*)&payload[3]);
            break;
        }

        case 0x02: { // AUTH
            const char *username = (char *)payload;
            size_t ulen = strlen(username);
            const char *display = (char *)(payload + ulen + 1);
            size_t dlen = strlen(display);
            const char *secret = (char *)(payload + ulen + 1 + dlen + 1);

            printf("  Username: %s\n", username);
            printf("  DisplayName: %s\n", display);
            printf("  Secret: %s\n", secret);
            break;
        }

        case 0x03: { // JOIN
            const char *channel = (char *)payload;
            size_t clen = strlen(channel);
            const char *display = (char *)(payload + clen + 1);
            printf("  ChannelID: %s\n", channel);
            printf("  DisplayName: %s\n", display);
            break;
        }

        case 0x04: // MSG
        case 0xFE: { // ERR (identical to MSG)
            const char *display = (char *)payload;
            size_t dlen = strlen(display);
            const char *message = (char *)(payload + dlen + 1);
            if (type == 0x04)
                printf("  [%s]: %s\n", display, message);
            else
                printf("  ERROR FROM %s: %s\n", display, message);
            break;
        }

        case 0xFF: { // BYE
            const char *display = (char *)payload;
            printf("  DisplayName: %s\n", display);
            break;
        }

        default:
            printf("  Unknown or unhandled type\n");
            break;
    }

    printf("=========================\n\n");
}
