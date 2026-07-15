# Run Log

**Experiment 1: Naive C Baseline**
* **Profile**: A
* **delay_ms**: 40
* **Miss %**: 100% (INVALID)
* **Overhead**: 1.02x
* **Changes/Why**: Ran the un-modified baseline code. The baseline does not implement any error correction, so any lost packet results in a deadline miss.

**Experiment 2: Hybrid FEC+ARQ with Delay 60ms**
* **Profile**: A
* **delay_ms**: 60
* **Miss %**: 0.67% (VALID)
* **Overhead**: 1.94x
* **Changes/Why**: Implemented a custom wire protocol. The sender attaches a redundant payload to 95% of packets (staying under the 2.0x limit). The receiver immediately forwards packets to the player and sends NACKs for any missing sequence numbers. The sender fulfills these NACKs in the redundant slot, or defaults to sending `seq - 1` proactively.

**Experiment 3: Stress Testing on Profile B (Moderate)**
* **Profile**: B
* **delay_ms**: 150
* **Miss %**: 1.20% (INVALID)
* **Overhead**: 1.94x
* **Changes/Why**: Tested the exact same code on a harsher network (5% loss, 80ms max jitter) with 150ms delay. It failed slightly above the 1% cap because NACKs themselves were being dropped or the recovery was taking longer than 150ms due to high round-trip times.

**Experiment 4: Robust Sliding Window NACKs**
* **Profile**: B
* **delay_ms**: 250
* **Miss %**: 0.20% (VALID)
* **Overhead**: 1.94x
* **Changes/Why**: Modified `receiver.c` to use a sliding window that re-sends NACKs periodically every 40ms for missing packets instead of just sending them once. This guarantees recovery even if NACKs are dropped. Increased delay to 250ms to account for the harsh 80ms network jitter which pushes total RTT close to 160ms. This easily beat the 1.00% cap!

**Experiment 5: WebRTC-style Pure Proactive FEC (Profile B)**
* **Profile**: B
* **delay_ms**: 100
* **Miss %**: 0.80% (VALID)
* **Overhead**: 1.96x
* **Changes/Why**: Completely removed the NACK feedback loop to avoid RTT delay dependencies (feedback overhead is now 0B). Implemented an adaptive token-bucket rate limiter paced at 314 bytes/frame to send primary packets and proactively schedule FEC packets (`seq - 1` and `seq - 2` fallback) whenever tokens allow. This reduced the delay threshold to 100ms on Profile B.

**Experiment 6: WebRTC-style Pure Proactive FEC (Profile A)**
* **Profile**: A
* **delay_ms**: 50
* **Miss %**: 1.00% (VALID)
* **Overhead**: 1.96x
* **Changes/Why**: Tested the pure FEC and token-bucket pacing architecture on Profile A. Lowered the delay budget to 50ms, achieving exactly 1.00% miss rate, passing the validity threshold.
