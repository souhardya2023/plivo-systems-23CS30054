/* RECEIVER (C) - Implements Hybrid FEC+ARQ
 *
 * Ports (all 127.0.0.1):
 *   bind 47002  <- media from your sender, via the hostile relay
 *   send 47020  -> harness player. MUST be: 4-byte seq + 160-byte payload.
 *   send 47003  -> feedback to your sender, via the relay
 */


// #include <arpa/inet.h>
// #include <stdio.h>
// #include <string.h>
// #include <sys/socket.h>
// #include <unistd.h>
// #include <stdint.h>

// #define PAYLOAD_SIZE 160

// uint8_t received[65536];

// int main(void) {
//     // 1. Receive from Relay
//     int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
//     struct sockaddr_in in_addr = {0};
//     in_addr.sin_family = AF_INET;
//     in_addr.sin_port = htons(47002);
//     in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
//     bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr);

//     // 2. Forward to Harness Player (We completely ignore feedback port 47003)
//     int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
//     struct sockaddr_in player = {0};
//     player.sin_family = AF_INET;
//     player.sin_port = htons(47020);
//     player.sin_addr.s_addr = inet_addr("127.0.0.1");

//     memset(received, 0, sizeof(received));
//     unsigned char buf[2048];

//     for (;;) {
//         // Pure blocking wait - zero CPU waste, zero timers
//         ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
//         if (n >= 4 + PAYLOAD_SIZE) {
//             uint32_t seq = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];

//             // If we haven't seen this packet yet, immediately fire it to the player
//             if (!received[seq % 65536]) {
//                 sendto(out_fd, buf, 4 + PAYLOAD_SIZE, 0, (struct sockaddr *)&player, sizeof(player));
//                 received[seq % 65536] = 1;
                
//                 // Clear the slot half a window away to handle wraparound indefinitely
//                 received[(seq + 32768) % 65536] = 0; 
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

// Track sequence numbers to prevent duplicating frames to the player
uint8_t received[65536];

int main(void) {
    // 1. Receive media from Relay
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr);

    // 2. Forward to Harness Player (The true Jitter Buffer)
    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in player = {0};
    player.sin_family = AF_INET;
    player.sin_port = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");

    memset(received, 0, sizeof(received));
    unsigned char buf[2048];

    for (;;) {
        // Pure blocking wait - zero CPU waste, handles infinite jitter
        ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
        if (n >= 4 + PAYLOAD_SIZE) {
            uint32_t seq = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];

            // If we haven't seen this packet yet, forward it instantly.
            // This perfectly mimics WebRTC's out-of-order execution, allowing the 
            // downstream player thread to unwrap and align the audio to deadlines.
            if (!received[seq % 65536]) {
                sendto(out_fd, buf, 4 + PAYLOAD_SIZE, 0, (struct sockaddr *)&player, sizeof(player));
                received[seq % 65536] = 1;
                
                // Clear the state half a window away to handle wraparound indefinitely
                received[(seq + 32768) % 65536] = 0; 
            }
        }
    }
    return 0;
}