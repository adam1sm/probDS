# probDS Benchmark Results (CPU Cache Boundary Sized)

This document lists performance benchmarks for the data structures implemented in the `probDS` library. All benchmarks were run on Apple Silicon hardware (macOS, C++17, Clang, `-O3 -march=native`) with pre-allocated datasets of `std::uint64_t` or `double` outside the timing loops to isolate the raw throughput and memory latency of each structure.

The benchmark scales are parameterized to target different CPU Cache boundaries:
-   **L1 Cache**: $N = 10^4$ ($10,000$ elements)
-   **L3 Cache**: $N = 10^6$ ($1,000,000$ elements)
-   **Main Memory**: $N = 5 \times 10^7$ ($50,000,000$ elements)

---

## 1. Set Membership Filters

These structures verify set membership or support elements deletion.

| Structure | Operation | N (Scale) | Time per Op (ns) | Throughput (M ops/sec) | Memory Footprint |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Bloom Filter** | Insert | 10K (L1) | 5.24 ns | 191.2 M/s | 16.00 KB |
|  |  | 1M (L3) | 9.67 ns | 103.5 M/s | 2.00 MB |
|  |  | 50M (RAM) | 34.05 ns | 29.4 M/s | 64.00 MB |
|  | Lookup (+) | 10K (L1) | 6.34 ns | 157.7 M/s | 16.00 KB |
|  |  | 1M (L3) | 9.54 ns | 104.9 M/s | 2.00 MB |
|  |  | 50M (RAM) | 31.58 ns | 31.7 M/s | 64.00 MB |
|  | Lookup (-) | 10K (L1) | 2.78 ns | 359.9 M/s | 16.00 KB |
|  |  | 1M (L3) | 19.45 ns | 51.4 M/s | 2.00 MB |
|  |  | 50M (RAM) | 36.44 ns | 27.5 M/s | 64.00 MB |
| **Cuckoo Filter** | Insert | 10K (L1) | 17.74 ns | 56.8 M/s | 32.00 KB |
|  |  | 1M (L3) | 27.78 ns | 36.0 M/s | 2.00 MB |
|  |  | 50M (RAM) | 55.82 ns | 18.6 M/s | 128.00 MB |
|  | Lookup (+) | 10K (L1) | 2.31 ns | 436.1 M/s | 32.00 KB |
|  |  | 1M (L3) | 18.68 ns | 53.6 M/s | 2.00 MB |
|  |  | 50M (RAM) | 24.82 ns | 40.3 M/s | 128.00 MB |
|  | Lookup (-) | 10K (L1) | 4.15 ns | 240.9 M/s | 32.00 KB |
|  |  | 1M (L3) | 12.96 ns | 77.2 M/s | 2.00 MB |
|  |  | 50M (RAM) | 35.21 ns | 28.4 M/s | 128.00 MB |
|  | Delete | 10K (L1) | 7.53 ns | 132.8 M/s | 32.00 KB |
|  |  | 1M (L3) | 22.40 ns | 45.2 M/s | 2.00 MB |
|  |  | 50M (RAM) | 30.35 ns | 33.2 M/s | 128.00 MB |
| **Counting Bloom** | Insert | 10K (L1) | 5.46 ns | 183.5 M/s | 128.00 KB |
|  |  | 1M (L3) | 15.61 ns | 64.2 M/s | 16.00 MB |
|  |  | 50M (RAM) | 42.38 ns | 24.4 M/s | 512.00 MB |
|  | Lookup (+) | 10K (L1) | 4.33 ns | 231.2 M/s | 128.00 KB |
|  |  | 1M (L3) | 11.33 ns | 88.5 M/s | 16.00 MB |
|  |  | 50M (RAM) | 27.06 ns | 37.1 M/s | 512.00 MB |
|  | Lookup (-) | 10K (L1) | 2.01 ns | 498.0 M/s | 128.00 KB |
|  |  | 1M (L3) | 19.96 ns | 50.2 M/s | 16.00 MB |
|  |  | 50M (RAM) | 35.70 ns | 28.1 M/s | 512.00 MB |
| **Scalable Bloom** | Insert | 10K (L1) | 8.72 ns | 114.8 M/s | Variable (dynamic) |
|  |  | 1M (L3) | 19.46 ns | 51.4 M/s |  |
|  |  | 50M (RAM) | 97.23 ns | 10.3 M/s |  |
|  | Lookup (+) | 10K (L1) | 9.36 ns | 107.0 M/s |  |
|  |  | 1M (L3) | 29.00 ns | 34.5 M/s |  |
|  |  | 50M (RAM) | 217.25 ns | 4.7 M/s |  |
|  | Lookup (-) | 10K (L1) | 9.67 ns | 107.7 M/s |  |
|  |  | 1M (L3) | 115.32 ns | 8.8 M/s |  |
|  |  | 50M (RAM) | 339.26 ns | 2.9 M/s |  |
| **Blocked Bloom** | Insert | 10K (L1) | 4.87 ns | 205.4 M/s | 64-byte block aligned |
|  |  | 1M (L3) | 7.31 ns | 136.9 M/s |  |
|  |  | 50M (RAM) | 20.08 ns | 50.0 M/s |  |
|  | Lookup (Scalar) | 10K (L1) | 4.55 ns | 219.9 M/s |  |
|  |  | 1M (L3) | 6.87 ns | 145.6 M/s |  |
|  |  | 50M (RAM) | 17.72 ns | 56.7 M/s |  |
|  | Lookup (SIMD) | 10K (L1) | 3.39 ns | 295.1 M/s |  |
|  |  | 1M (L3) | 4.41 ns | 227.0 M/s |  |
|  |  | 50M (RAM) | 13.31 ns | 75.3 M/s |  |
|  | Lookup (Batch, N=1) | 10K (L1) | 4.64 ns | 215.7 M/s |  |
|  |  | 1M (L3) | 6.80 ns | 147.1 M/s |  |
|  |  | 50M (RAM) | 17.40 ns | 57.6 M/s |  |
|  | Lookup (Batch, N=4) | 10K (L1) | 5.99 ns | 166.9 M/s |  |
|  |  | 1M (L3) | 7.92 ns | 126.3 M/s |  |
|  |  | 50M (RAM) | 15.60 ns | 64.2 M/s |  |
|  | Lookup (Batch, N=8) | 10K (L1) | 5.52 ns | 182.1 M/s |  |
|  |  | 1M (L3) | 6.78 ns | 147.6 M/s |  |
|  |  | 50M (RAM) | 13.02 ns | 76.9 M/s |  |
|  | Lookup (Batch, N=16) | 10K (L1) | 5.44 ns | 184.8 M/s |  |
|  |  | 1M (L3) | 6.56 ns | 152.5 M/s |  |
|  |  | 50M (RAM) | 10.76 ns | 93.1 M/s |  |
| **Quotient Filter** | Insert | 10K (L1) | 3.29 ns | 305.6 M/s | Slot-packed |
|  |  | 1M (L3) | 6.84 ns | 146.3 M/s |  |
|  |  | 50M (RAM) | 33.36 ns | 30.0 M/s |  |
|  | Lookup (+) | 10K (L1) | 3.05 ns | 327.7 M/s |  |
|  |  | 1M (L3) | 7.32 ns | 137.0 M/s |  |
|  |  | 50M (RAM) | 54.18 ns | 18.5 M/s |  |
|  | Lookup (-) | 10K (L1) | 2.13 ns | 470.5 M/s |  |
|  |  | 1M (L3) | 6.54 ns | 153.0 M/s |  |
|  |  | 50M (RAM) | 37.23 ns | 26.9 M/s |  |
|  | Delete | 10K (L1) | 7.68 ns | 260.4 M/s |  |
|  |  | 1M (L3) | 19.21 ns | 104.2 M/s |  |
|  |  | 50M (RAM) | 130.97 ns | 15.3 M/s |  |
| **Binary Fuse (8-bit)** | Insert | 10K (L1) | 20.99 ns | 47.7 M/s | 12.56 KB |
|  |  | 1M (L3) | 35.15 ns | 28.5 M/s | 1.08 MB |
|  |  | 50M (RAM) | 48.11 ns | 21.0 M/s | 53.69 MB |
|  | Lookup (+) | 10K (L1) | 2.56 ns | 390.9 M/s | 12.56 KB |
|  |  | 1M (L3) | 3.26 ns | 306.8 M/s | 1.08 MB |
|  |  | 50M (RAM) | 14.66 ns | 68.2 M/s | 53.69 MB |
|  | Lookup (-) | 10K (L1) | 2.59 ns | 387.1 M/s | 12.56 KB |
|  |  | 1M (L3) | 3.26 ns | 306.9 M/s | 1.08 MB |
|  |  | 50M (RAM) | 14.88 ns | 67.3 M/s | 53.69 MB |
| **XorFilter (8-bit)** | Insert | 10K (L1) | 16.50 ns | 60.7 M/s | 24.05 KB |
|  |  | 1M (L3) | 42.48 ns | 23.6 M/s | 1.50 MB |
|  |  | 50M (RAM) | 96.12 ns | 10.5 M/s | 96.00 MB |
|  | Lookup (+) | 10K (L1) | 2.63 ns | 380.7 M/s | 24.05 KB |
|  |  | 1M (L3) | 3.24 ns | 309.7 M/s | 1.50 MB |
|  |  | 50M (RAM) | 15.30 ns | 65.5 M/s | 96.00 MB |
|  | Lookup (-) | 10K (L1) | 2.75 ns | 366.2 M/s | 24.05 KB |
|  |  | 1M (L3) | 3.24 ns | 309.7 M/s | 1.50 MB |
|  |  | 50M (RAM) | 15.32 ns | 65.4 M/s | 96.00 MB |
| **RibbonFilter (8-bit)** | Insert | 10K (L1) | 154.92 ns | 6.5 M/s | 16.12 KB |
|  |  | 1M (L3) | 173.27 ns | 5.8 M/s | 2.00 MB |
|  |  | 50M (RAM) | 207.13 ns | 5.0 M/s | 64.00 MB |
|  | Lookup (+) | 10K (L1) | 4.62 ns | 216.7 M/s | 16.12 KB |
|  |  | 1M (L3) | 6.02 ns | 166.5 M/s | 2.00 MB |
|  |  | 50M (RAM) | 26.21 ns | 38.2 M/s | 64.00 MB |
|  | Lookup (-) | 10K (L1) | 4.71 ns | 212.1 M/s | 16.12 KB |
|  |  | 1M (L3) | 6.18 ns | 161.9 M/s | 2.00 MB |
|  |  | 50M (RAM) | 26.22 ns | 38.2 M/s | 64.00 MB |
| **Learned Bloom Filter** | Insert | 10K (L1) | 2.65 ns | 376.7 M/s | Variable (dynamic) |
|  |  | 1M (L3) | 6.10 ns | 164.0 M/s |  |
|  |  | 50M (RAM) | 14.19 ns | 70.8 M/s |  |
|  | Lookup (+) | 10K (L1) | 2.79 ns | 358.8 M/s |  |
|  |  | 1M (L3) | 5.77 ns | 173.3 M/s |  |

---

## 2. Cardinality & Frequency Sketches

These structures estimate set size or element frequency.

| Structure | Operation | N (Scale) | Time per Op | Throughput | Memory Footprint |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **HyperLogLog** | Insert | 10K (L1) | 1.24 ns | 804.2 M/s | 16.00 KB |
|  |  | 1M (L3) | 1.25 ns | 810.0 M/s | 16.00 KB |
|  |  | 50M (RAM) | 0.85 ns | 1.2 G/s | 16.00 KB |
|  | Estimate | 10K (L1) | 0.27 ns | 3.8 G/s | 16.00 KB |
|  |  | 1M (L3) | 0.25 ns | 4.0 G/s | 16.00 KB |
|  |  | 50M (RAM) | 0.25 ns | 4.0 G/s | 16.00 KB |
|  | Merge | 10K (L1) | 3.79 us | 264.2 K/s | 16.00 KB |
|  |  | 1M (L3) | 3.78 us | 265.0 K/s | 16.00 KB |
|  |  | 50M (RAM) | 3.77 us | 264.9 K/s | 16.00 KB |
| **Count-Min Sketch** | Insert | 10K (L1) | 5.82 ns | 172.2 M/s | 20.00 KB |
|  |  | 1M (L3) | 5.93 ns | 170.2 M/s | 20.00 KB |
|  |  | 50M (RAM) | 5.81 ns | 172.5 M/s | 20.00 KB |
|  | Estimate | 10K (L1) | 4.29 ns | 233.2 M/s | 20.00 KB |
|  |  | 1M (L3) | 4.20 ns | 238.4 M/s | 20.00 KB |
|  |  | 50M (RAM) | 4.37 ns | 230.8 M/s | 20.00 KB |
| **Count-Min Log** | Insert | 10K (L1) | 8.04 ns | 124.5 M/s | 1.3 KB (Morris 4-bit) |
|  |  | 1M (L3) | 8.81 ns | 113.6 M/s | 1.3 KB |
|  |  | 50M (RAM) | 8.86 ns | 113.0 M/s | 1.3 KB |
|  | Estimate | 10K (L1) | 6.05 ns | 165.4 M/s |  |
|  |  | 1M (L3) | 5.92 ns | 169.1 M/s |  |
|  |  | 50M (RAM) | 5.91 ns | 169.3 M/s |  |
| **Count Sketch** | Insert | 10K (L1) | 9.70 ns | 103.2 M/s | 20.5 KB |
|  |  | 1M (L3) | 9.61 ns | 104.2 M/s | 20.5 KB |
|  |  | 50M (RAM) | 9.59 ns | 104.4 M/s | 20.5 KB |
|  | Estimate | 10K (L1) | 10.96 ns | 91.4 M/s |  |
|  |  | 1M (L3) | 10.90 ns | 92.4 M/s |  |
|  |  | 50M (RAM) | 10.61 ns | 94.3 M/s |  |
| **AMS Sketch** | Insert | 10K (L1) | 4.06 ns | 247.1 M/s | 5x64 counters |
|  |  | 1M (L3) | 3.96 ns | 252.8 M/s |  |
|  |  | 50M (RAM) | 3.98 ns | 251.7 M/s |  |
|  | Estimate F2 | 10K (L1) | 164.52 ns | 6.1 M/s |  |
|  |  | 1M (L3) | 160.30 ns | 6.2 M/s |  |
|  |  | 50M (RAM) | 162.28 ns | 6.2 M/s |  |
| **Exponential Hist.** | Update | 10K (L1) | 37.33 ns | 26.8 M/s | Variable (dynamic) |
|  |  | 1M (L3) | 50.23 ns | 19.9 M/s |  |
|  |  | 50M (RAM) | 52.84 ns | 19.0 M/s |  |
|  | Estimate | 10K (L1) | 26.04 ns | 38.5 M/s |  |
|  |  | 1M (L3) | 38.94 ns | 25.7 M/s |  |
|  |  | 50M (RAM) | 38.85 ns | 25.8 M/s |  |

---

## 3. Estimators, Quantiles & Heavy Hitters

These structures estimate set similarities, percentiles, heavy hitters, or samples.

| Structure | Operation | Configuration / Scale | Time per Op | Throughput |
| :--- | :--- | :--- | :--- | :--- |
| **MinHash** | Insert | 10K (L1) | 32.31 ns | 31.0 M/s |
|  |  | 1M (L3) | 32.61 ns | 30.7 M/s |
|  |  | 50M (RAM) | 33.16 ns | 30.2 M/s |
|  | Jaccard Query | k=128 | 16.55 ns | 60.4 M/s |
|  |  | k=256 | 32.50 ns | 30.8 M/s |
|  |  | k=512 | 64.52 ns | 15.5 M/s |
|  |  | k=1024 | 131.08 ns | 7.6 M/s |
| **Weighted MinHash** | Insert | 10K (L1) | 3269.51 ns | 306.2 K/s |
|  |  | 1M (L3) | 3288.08 ns | 304.8 K/s |
|  |  | 50M (RAM) | 3284.22 ns | 304.8 K/s |
|  | Jaccard Query | k=128 | 41.86 ns | 23.9 M/s |
|  |  | k=256 | 83.19 ns | 12.0 M/s |
|  |  | k=512 | 175.07 ns | 5.7 M/s |
|  |  | k=1024 | 358.05 ns | 2.8 M/s |
| **b-Bit MinHash** | Insert | 10K (L1) | 34.45 ns | 29.0 M/s |
|  |  | 1M (L3) | 34.29 ns | 29.3 M/s |
|  |  | 50M (RAM) | 33.35 ns | 30.0 M/s |
|  | Jaccard Query | k=128 | 37.07 ns | 27.0 M/s |
|  |  | k=256 | 73.00 ns | 13.7 M/s |
|  |  | k=512 | 145.09 ns | 6.9 M/s |
|  |  | k=1024 | 288.99 ns | 3.5 M/s |
| **OddSketch** | Insert | 10K (L1) | 0.73 ns | 1.37 G/s |
|  |  | 1M (L3) | 0.71 ns | 1.42 G/s |
|  |  | 50M (RAM) | 0.73 ns | 1.37 G/s |
|  | Symmetric Diff | m=1024 | 7.65 ns | 130.8 M/s |
|  |  | m=2048 | 10.50 ns | 95.2 M/s |
|  |  | m=4096 | 16.62 ns | 60.2 M/s |
|  |  | m=8192 | 27.86 ns | 35.9 M/s |
|  |  | m=16384 | 48.82 ns | 20.5 M/s |
|  |  | m=32768 | 80.90 ns | 12.4 M/s |
|  |  | m=65536 | 146.84 ns | 6.8 M/s |
| **SimHash** | Insert | 10K (L1) | 18.22 ns | 54.9 M/s |
|  |  | 1M (L3) | 18.38 ns | 54.4 M/s |
|  |  | 50M (RAM) | 18.48 ns | 54.3 M/s |
|  | Get Fingerprint | 10K (L1) | 6.08 ns | 164.6 M/s |
|  |  | 1M (L3) | 6.05 ns | 165.3 M/s |
|  |  | 50M (RAM) | 6.06 ns | 165.1 M/s |
| **t-Digest** | Insert | 10K (L1) | 12.53 ns | 79.9 M/s |
|  |  | 1M (L3) | 50.11 ns | 20.0 M/s |
|  |  | 50M (RAM) | 52.57 ns | 19.0 M/s |
|  | Quantile Query | 10K (L1) | 262.70 ns | 3.8 M/s |
|  |  | 1M (L3) | 471.38 ns | 2.1 M/s |
|  |  | 50M (RAM) | 618.50 ns | 1.6 M/s |
| **KLL Sketch** | Insert | 10K (L1) | 18.64 ns | 53.7 M/s |
|  |  | 1M (L3) | 76.47 ns | 13.1 M/s |
|  |  | 50M (RAM) | 118.63 ns | 8.4 M/s |
|  | Quantile Query | 10K (L1) | 105.77 ns | 9.5 M/s |
|  |  | 1M (L3) | 46.68 ns | 21.4 M/s |
|  |  | 50M (RAM) | 126.09 ns | 7.9 M/s |
| **DDSketch** | Insert | 10K (L1) | 4.70 ns | 213.0 M/s |
|  |  | 1M (L3) | 4.54 ns | 220.7 M/s |  |
|  |  | 50M (RAM) | 4.53 ns | 220.9 M/s |  |
| **Misra-Gries** | Insert | 10K (L1) | 32.66 ns | 30.6 M/s |
|  |  | 1M (L3) | 32.74 ns | 30.5 M/s |
|  |  | 50M (RAM) | 32.70 ns | 30.6 M/s |
| **Space-Saving** | Insert | 10K (L1) | 44.75 ns | 22.4 M/s |
|  |  | 1M (L3) | 44.79 ns | 22.3 M/s |
|  |  | 50M (RAM) | 44.57 ns | 22.4 M/s |
| **HeavyKeeper** | Insert | 10K (L1) | 11.86 ns | 84.4 M/s |
|  |  | 1M (L3) | 17.44 ns | 57.4 M/s |
|  |  | 50M (RAM) | 17.40 ns | 57.5 M/s |
| **Reservoir Sampler** | Insert | 10K (L1) | 4.11 ns | 243.6 M/s |
|  |  | 1M (L3) | 7.71 ns | 129.7 M/s |
|  |  | 50M (RAM) | 8.65 ns | 115.6 M/s |
| **Lossy Counting** | Insert | 10K (L1) | 28.99 ns | 34.5 M/s |
|  |  | 1M (L3) | 29.07 ns | 34.4 M/s |
|  |  | 50M (RAM) | 29.04 ns | 34.4 M/s |

---

## 4. Concurrent & Infrastructure Structures

These structures provide thread-safe concurrent access and recommendation logic.

### Concurrent Performance (Insertion throughput at scale N=10,000)

| Structure | Threads | Time per Insert (ns) | Throughput (M ops/sec) |
| :--- | :--- | :--- | :--- |
| **Concurrent Bloom Filter** | 1 | 12.69 ns | 78.8 M/s |
|  | 2 | 14.09 ns | 71.0 M/s |
|  | 4 | 10.35 ns | 96.6 M/s |
|  | 8 | 24.60 ns | 40.6 M/s |
| **Concurrent HyperLogLog** | 1 | 5.01 ns | 199.5 M/s |
|  | 2 | 6.74 ns | 148.3 M/s |
|  | 4 | 5.61 ns | 178.1 M/s |
|  | 8 | 8.21 ns | 121.8 M/s |
| **Concurrent Count-Min** | 1 | 7.67 ns | 130.3 M/s |
|  | 2 | 25.91 ns | 38.6 M/s |
|  | 4 | 19.97 ns | 50.1 M/s |
|  | 8 | 29.05 ns | 34.4 M/s |
