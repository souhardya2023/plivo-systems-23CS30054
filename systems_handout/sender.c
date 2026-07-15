/* SENDER (C) - Implements Hybrid FEC+ARQ
 *
 * Ports (all 127.0.0.1):
 *   bind 47010  <- harness source delivers frame i here at t0 + i*20ms
 *   send 47001  -> relay uplink toward the receiver
 *   bind 47004  <- feedback from receiver, via the relay
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>

#define PAYLOAD_SIZE 160
#define HISTORY_SIZE 256
#define NACK_QUEUE_SIZE 1024

struct {
    uint32_t seq;
    unsigned char payload[PAYLOAD_SIZE];
    int valid;
} history[HISTORY_SIZE];

uint32_t nack_queue[NACK_QUEUE_SIZE];
int nack_head = 0;
int nack_tail = 0;

void push_nack(uint32_t seq) {
    int next = (nack_tail + 1) % NACK_QUEUE_SIZE;
    if (next != nack_head) {
        nack_queue[nack_tail] = seq;
        nack_tail = next;
    }
}

int pop_nack(uint32_t *seq) {
    if (nack_head == nack_tail) return 0;
    *seq = nack_queue[nack_head];
    nack_head = (nack_head + 1) % NACK_QUEUE_SIZE;
    return 1;
}

int main(void) {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47010);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr) < 0) {
        perror("bind 47010");
        return 1;
    }

    int fb_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in fb_addr = {0};
    fb_addr.sin_family = AF_INET;
    fb_addr.sin_port = htons(47004);
    fb_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(fb_fd, (struct sockaddr *)&fb_addr, sizeof fb_addr) < 0) {
        perror("bind 47004");
        return 1;
    }
    fcntl(fb_fd, F_SETFL, O_NONBLOCK); // Make feedback non-blocking

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay = {0};
    relay.sin_family = AF_INET;
    relay.sin_port = htons(47001);
    relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    unsigned char buf[2048];
    uint64_t total_bytes_sent = 0;
    uint64_t total_frames_sent = 0;
    const uint64_t budget_per_frame = 310; // Max allowed is ~320, we keep it under

    memset(history, 0, sizeof(history));

    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
        if (n <= 0) continue;

        // Drain NACKs
        unsigned char nack_buf[4];
        while (recvfrom(fb_fd, nack_buf, sizeof nack_buf, 0, NULL, NULL) == 4) {
            uint32_t nack_seq = (nack_buf[0] << 24) | (nack_buf[1] << 16) | (nack_buf[2] << 8) | nack_buf[3];
            push_nack(nack_seq);
        }

        if (n < 4 + PAYLOAD_SIZE) continue;

        uint32_t seq = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
        
        int h_idx = seq % HISTORY_SIZE;
        history[h_idx].seq = seq;
        memcpy(history[h_idx].payload, buf + 4, PAYLOAD_SIZE);
        history[h_idx].valid = 1;

        unsigned char out_buf[1024];
        out_buf[0] = (seq >> 24) & 0xFF;
        out_buf[1] = (seq >> 16) & 0xFF;
        out_buf[2] = (seq >> 8) & 0xFF;
        out_buf[3] = seq & 0xFF;
        out_buf[4] = 0; // Default: no redundant payload
        
        memcpy(out_buf + 5, buf + 4, PAYLOAD_SIZE);
        int packet_len = 4 + 1 + PAYLOAD_SIZE;
        int red_len = 1 + PAYLOAD_SIZE; // offset + payload

        static uint64_t last_sent[HISTORY_SIZE] = {0};

        // Check if we can afford a redundant packet
        if (total_bytes_sent + packet_len + red_len <= (total_frames_sent + 1) * budget_per_frame) {
            uint32_t target_seq = seq;
            int found_redundant = 0;

            // 1. Try to fulfill a NACK
            uint32_t nack_seq;
            while (pop_nack(&nack_seq)) {
                if (seq > nack_seq && (seq - nack_seq) <= 255) {
                    int n_idx = nack_seq % HISTORY_SIZE;
                    if (history[n_idx].valid && history[n_idx].seq == nack_seq) {
                        if (total_frames_sent - last_sent[n_idx] >= 2) { // deduplicate
                            target_seq = nack_seq;
                            found_redundant = 1;
                            break;
                        }
                    }
                }
            }

            // 2. Default proactive FEC (send seq - 1)
            if (!found_redundant && seq >= 1) {
                target_seq = seq - 1;
                int n_idx = target_seq % HISTORY_SIZE;
                if (history[n_idx].valid && history[n_idx].seq == target_seq) {
                    found_redundant = 1;
                }
            }
            
            // 3. Fallback proactive FEC (send seq - 2)
            if (!found_redundant && seq >= 2) {
                target_seq = seq - 2;
                int n_idx = target_seq % HISTORY_SIZE;
                if (history[n_idx].valid && history[n_idx].seq == target_seq) {
                    found_redundant = 1;
                }
            }

            if (found_redundant) {
                out_buf[4] = 1;
                out_buf[packet_len] = (seq - target_seq) & 0xFF;
                int n_idx = target_seq % HISTORY_SIZE;
                memcpy(out_buf + packet_len + 1, history[n_idx].payload, PAYLOAD_SIZE);
                packet_len += red_len;
                last_sent[n_idx] = total_frames_sent;
            }
        }

        sendto(out_fd, out_buf, (size_t)packet_len, 0, (struct sockaddr *)&relay, sizeof relay);
        total_bytes_sent += packet_len;
        total_frames_sent++;
    }
    return 0;
}
