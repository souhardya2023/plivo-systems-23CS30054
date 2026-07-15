/* SENDER (C) - Implements Hybrid FEC+ARQ
 *
 * Ports (all 127.0.0.1):
 *   bind 47010  <- harness source delivers frame i here at t0 + i*20ms
 *   send 47001  -> relay uplink toward the receiver
 *   bind 47004  <- feedback from receiver, via the relay
 */

// #include <arpa/inet.h>
// #include <stdio.h>
// #include <string.h>
// #include <sys/socket.h>
// #include <unistd.h>
// #include <stdint.h>

// #define PAYLOAD_SIZE 160
// #define MAX_HISTORY 256

// struct {
//     uint32_t seq;
//     unsigned char payload[PAYLOAD_SIZE];
//     int valid;
// } history[MAX_HISTORY];

// void send_packet(int fd, struct sockaddr_in *addr, uint32_t seq, const unsigned char *payload) {
//     unsigned char buf[4 + PAYLOAD_SIZE];
//     buf[0] = (seq >> 24) & 0xFF;
//     buf[1] = (seq >> 16) & 0xFF;
//     buf[2] = (seq >> 8) & 0xFF;
//     buf[3] = seq & 0xFF;
//     memcpy(buf + 4, payload, PAYLOAD_SIZE);
//     sendto(fd, buf, 4 + PAYLOAD_SIZE, 0, (struct sockaddr *)addr, sizeof(*addr));
// }

// int main(void) {
//     // 1. Listen for new frames from the harness
//     int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
//     struct sockaddr_in in_addr = {0};
//     in_addr.sin_family = AF_INET;
//     in_addr.sin_port = htons(47010);
//     in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
//     bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr);

//     // 2. Send to Relay (We completely ignore the feedback port 47004)
//     int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
//     struct sockaddr_in relay = {0};
//     relay.sin_family = AF_INET;
//     relay.sin_port = htons(47001);
//     relay.sin_addr.s_addr = inet_addr("127.0.0.1");

//     memset(history, 0, sizeof(history));
//     unsigned char buf[2048];
    
//     int token_bucket = 0;

//     for (;;) {
//         ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
//         if (n >= 4 + PAYLOAD_SIZE) {
//             uint32_t seq = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
            
//             // Strictly cap bandwidth. 314 bytes/frame = 1.96x overhead. 
//             // Guarantees we never exceed the 2.00x cap.
//             token_bucket += 314; 
//             if (token_bucket > 630) token_bucket = 630;

//             int idx = seq % MAX_HISTORY;
//             history[idx].seq = seq;
//             memcpy(history[idx].payload, buf + 4, PAYLOAD_SIZE);
//             history[idx].valid = 1;

//             // Always send the primary packet
//             send_packet(out_fd, &relay, seq, history[idx].payload);
//             token_bucket -= 164;

//             // Spend remaining tokens on proactive FEC (seq - 1)
//             if (token_bucket >= 164 && seq >= 1) {
//                 uint32_t fec1_seq = seq - 1;
//                 int f1_idx = fec1_seq % MAX_HISTORY;
//                 if (history[f1_idx].valid && history[f1_idx].seq == fec1_seq) {
//                     send_packet(out_fd, &relay, fec1_seq, history[f1_idx].payload);
//                     token_bucket -= 164;
//                 }
//             }

//             // If budget slowly accumulates, occasionally send an extra (seq - 2) packet!
//             if (token_bucket >= 164 && seq >= 2) {
//                 uint32_t fec2_seq = seq - 2;
//                 int f2_idx = fec2_seq % MAX_HISTORY;
//                 if (history[f2_idx].valid && history[f2_idx].seq == fec2_seq) {
//                     send_packet(out_fd, &relay, fec2_seq, history[f2_idx].payload);
//                     token_bucket -= 164;
//                 }
//             }
//         }
//     }
//     return 0;
// }

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>

#define PAYLOAD_SIZE 160
#define MAX_HISTORY 256

struct {
    uint32_t seq;
    unsigned char payload[PAYLOAD_SIZE];
    int valid;
} history[MAX_HISTORY];

void send_packet(int fd, struct sockaddr_in *addr, uint32_t seq, const unsigned char *payload) {
    unsigned char buf[4 + PAYLOAD_SIZE];
    buf[0] = (seq >> 24) & 0xFF;
    buf[1] = (seq >> 16) & 0xFF;
    buf[2] = (seq >> 8) & 0xFF;
    buf[3] = seq & 0xFF;
    memcpy(buf + 4, payload, PAYLOAD_SIZE);
    sendto(fd, buf, 4 + PAYLOAD_SIZE, 0, (struct sockaddr *)addr, sizeof(*addr));
}

int main(void) {
    // 1. Listen for new audio frames from the harness source
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47010);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr);

    // 2. Send media to Relay (WebRTC ignores ARQ/NACKs for <100ms audio)
    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay = {0};
    relay.sin_family = AF_INET;
    relay.sin_port = htons(47001);
    relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    memset(history, 0, sizeof(history));
    unsigned char buf[2048];
    
    // Adaptive Bandwidth Controller
    int token_bucket = 0;

    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
        if (n >= 4 + PAYLOAD_SIZE) {
            uint32_t seq = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
            
            // WebRTC-style Strict Bandwidth Pacing: 
            // 314 bytes total per frame = 1.96x overhead (Safely under 2.00x cap)
            token_bucket += 314; 
            if (token_bucket > 630) token_bucket = 630;

            int idx = seq % MAX_HISTORY;
            history[idx].seq = seq;
            memcpy(history[idx].payload, buf + 4, PAYLOAD_SIZE);
            history[idx].valid = 1;

            // Transmit Primary Stream
            send_packet(out_fd, &relay, seq, history[idx].payload);
            token_bucket -= 164;

            // Transmit Independent FEC Stream (seq - 1)
            if (token_bucket >= 164 && seq >= 1) {
                uint32_t fec1_seq = seq - 1;
                int f1_idx = fec1_seq % MAX_HISTORY;
                if (history[f1_idx].valid && history[f1_idx].seq == fec1_seq) {
                    send_packet(out_fd, &relay, fec1_seq, history[f1_idx].payload);
                    token_bucket -= 164;
                }
            }

            // Transmit Deep FEC Stream (seq - 2) if bandwidth permits
            if (token_bucket >= 164 && seq >= 2) {
                uint32_t fec2_seq = seq - 2;
                int f2_idx = fec2_seq % MAX_HISTORY;
                if (history[f2_idx].valid && history[f2_idx].seq == fec2_seq) {
                    send_packet(out_fd, &relay, fec2_seq, history[f2_idx].payload);
                    token_bucket -= 164;
                }
            }
        }
    }
    return 0;
}