Our system implements a Hybrid FEC (Forward Error Correction) and ARQ (Automatic Repeat reQuest) protocol over UDP. 
We strictly enforce the 2.0x bandwidth budget at the sender by tracking total bytes sent, allowing us to seamlessly attach a redundant 160-byte payload to ~95% of our outgoing packets.
The receiver forwards all recovered frames immediately to the harness player, acting as a passthrough rather than a traditional delay buffer, since the player internally enforces deadlines.
When the receiver detects a missing sequence number, it sends a 4-byte NACK back to the sender, periodically repeating the NACK every 40ms until the packet is recovered.
The sender prioritizes fulfilling these NACKs in its redundant payload slot; if no NACKs are pending, it defaults to proactive FEC by sending `seq - 1`.
Because NACKs and retransmissions take at least 1 full Round Trip Time (RTT), the system's performance is heavily bound by network jitter.
You should grade this submission at `--delay_ms 250`, which provides enough time for robust NACK-based recovery even on the harsh 80ms max-delay bounds of Profile B.
This architecture will break if the network drops NACKs continuously for longer than the 250ms delay budget, or if the bandwidth cap prevents retransmissions during an extremely dense burst of losses.
