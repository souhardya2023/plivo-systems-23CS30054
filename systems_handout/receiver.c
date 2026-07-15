/* RECEIVER (C) - Implements Hybrid FEC+ARQ
 *
 * Ports (all 127.0.0.1):
 *   bind 47002  <- media from your sender, via the hostile relay
 *   send 47020  -> harness player. MUST be: 4-byte seq + 160-byte payload.
 *   send 47003  -> feedback to your sender, via the relay
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>

#define PAYLOAD_SIZE 160

uint8_t received[65536];
uint32_t highest_seq = 0;
int first_packet = 1;

uint32_t last_nack_time[65536];
uint32_t current_time = 0;

void mark_received(uint32_t seq) {
    received[seq % 65536] = 1;
}

int is_received(uint32_t seq) {
    return received[seq % 65536];
}

void forward_to_player(int out_fd, struct sockaddr_in *player, uint32_t seq, const unsigned char *payload) {
    unsigned char buf[4 + PAYLOAD_SIZE];
    buf[0] = (seq >> 24) & 0xFF;
    buf[1] = (seq >> 16) & 0xFF;
    buf[2] = (seq >> 8) & 0xFF;
    buf[3] = seq & 0xFF;
    memcpy(buf + 4, payload, PAYLOAD_SIZE);
    sendto(out_fd, buf, 4 + PAYLOAD_SIZE, 0, (struct sockaddr *)player, sizeof(*player));
    mark_received(seq);
}

int main(void) {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr) < 0) {
        perror("bind 47002");
        return 1;
    }

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in player = {0};
    player.sin_family = AF_INET;
    player.sin_port = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");

    int fb_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay_fb = {0};
    relay_fb.sin_family = AF_INET;
    relay_fb.sin_port = htons(47003);
    relay_fb.sin_addr.s_addr = inet_addr("127.0.0.1");

    unsigned char buf[2048];
    memset(received, 0, sizeof(received));

    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
        if (n < 4 + 1 + PAYLOAD_SIZE) continue;

        uint32_t seq = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
        uint8_t has_redundant = buf[4];

        if (first_packet) {
            highest_seq = seq;
            first_packet = 0;
            // The harness usually starts seq at 0.
            for (uint32_t i = 0; i <= seq; i++) {
                mark_received(i);
            }
        }

        // Forward primary
        if (!is_received(seq)) {
            forward_to_player(out_fd, &player, seq, buf + 5);
        }

        // Forward redundant
        if (has_redundant == 1 && n >= 4 + 1 + PAYLOAD_SIZE + 1 + PAYLOAD_SIZE) {
            uint8_t offset = buf[4 + 1 + PAYLOAD_SIZE];
            if (offset > 0 && offset <= seq) {
                uint32_t red_seq = seq - offset;
                if (!is_received(red_seq)) {
                    forward_to_player(out_fd, &player, red_seq, buf + 4 + 1 + PAYLOAD_SIZE + 1);
                }
            }
        }

        current_time++;

        // Periodic NACKs for the sliding window of the last 100 packets
        uint32_t start_seq = (highest_seq >= 100) ? highest_seq - 100 : 0;
        for (uint32_t i = start_seq; i < highest_seq; i++) {
            if (!is_received(i) && (current_time - last_nack_time[i % 65536] >= 2)) {
                unsigned char nack_buf[4];
                nack_buf[0] = (i >> 24) & 0xFF;
                nack_buf[1] = (i >> 16) & 0xFF;
                nack_buf[2] = (i >> 8) & 0xFF;
                nack_buf[3] = i & 0xFF;
                sendto(fb_fd, nack_buf, 4, 0, (struct sockaddr *)&relay_fb, sizeof relay_fb);
                last_nack_time[i % 65536] = current_time;
            }
        }

        // Update highest_seq
        if (seq > highest_seq) {
            // Clear future window to prevent wrap-around issues
            for (uint32_t i = highest_seq + 1; i <= seq; i++) {
                received[(i + 200) % 65536] = 0; 
                last_nack_time[(i + 200) % 65536] = 0; // also clear NACK time
            }
            highest_seq = seq;
        }
    }
    return 0;
}
