# probDS
High-performance, production-grade probabilistic data structures for C++17.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg) ![Header-only](https://img.shields.io/badge/Header--only-Header--only-orange.svg) ![MIT License](https://img.shields.io/badge/License-MIT-green.svg)

---
## What are probabilistic data structures

Probabilistic data structures trade absolute exactness for massive, sub-linear reductions in memory footprint and latency. In high-throughput streaming environments or distributed storage subsystems, exact indexing scales poorly. A standard hash set (`std::unordered_set`) storing C-style strings or integers requires a raw heap allocation per entry, overhead pointers, and key storage, translating to roughly 500 bits per element. In contrast, a Bloom filter operating at a 1% false positive rate requires only 9.6 bits per element ($m/n \approx 9.6$), keeping set membership validation inside CPU L1/L2 caches. For cardinality counting, tracking a billion unique elements in a hash set would consume over 30 GB of RAM, whereas a HyperLogLog sketch estimates the same cardinality with an empirical relative error under 0.81% using a fixed 16 KB register block.

In modern production systems, this tradeoff is a core architectural pattern rather than an optimization detail. RocksDB evaluates a Bloom filter for every SSTable before issuing disk reads, eliminating approximately 90% of costly random block reads for non-existent keys, reducing I/O by ~90% on read-heavy workloads. Redis natively embeds the HyperLogLog algorithm via the `PFADD` and `PFCOUNT` primitives to compute active user sessions on streaming keys with minimal memory allocation. CDN nodes at Akamai route incoming requests through Bloom filters to dynamically track whether a URL has been requested previously: only elements returning positive set membership are designated for eviction-based RAM caching, safeguarding the high-frequency CDN cache from being polluted by one-off requests. A single missed cache on a CDN node serving millions of requests per second cannot afford a hash table lookup.

Most production usage is either hand-rolled inside a larger system or uses language-specific libraries with no benchmarking transparency. probDS is the only C++ library that implements the complete academic literature on probabilistic data structures.

---
## Structure decision tree

Use the following decision tree to identify the correct structure for your architectural constraints:

```
probDS Core Architecture
├── Set Membership Filters (Do you need to check if an element exists?)
│   ├── Static Filter (Is the set built once and never updated after construction?)
│   │   ├── Memory constrained (Minimal bits/element) ───────────────────► RibbonFilter
│   │   └── Query speed constrained (Minimal nanoseconds/lookup) ────────► BinaryFuseFilter / XorFilter
│   └── Dynamic Filter (Does the set support runtime updates?)
│       ├── Requires deletion support
│       │   ├── High load factors (> 90% space occupancy) ────────────────► CuckooFilter
│       │   └── Fixed table footprint, contiguous storage ───────────────► QuotientFilter
│       └── No deletion support
│           ├── Cache-line alignment (SIMD vectorized lookup) ───────────► BlockedBloomFilter
│           ├── Standard use case (Morris double hashing) ───────────────► BloomFilter
│           └── Set size grows unboundedly at runtime ───────────────────► ScalableBloomFilter
│
├── Cardinality Estimation (Do you need to count unique items?)
│   └── Fixed 16 KB memory limit, high-volume stream ────────────────────► HyperLogLog
│
├── Frequency Sketches (Do you need to estimate how often items occur?)
│   ├── Unbiased estimation (No systematic overestimation) ──────────────► CountSketch
│   └── Overestimate-only bias (Frequency is never underestimated)
│       ├── Memory constrained (4-bit counter registers) ──────────────► CountMinLog
│       └── Latency critical (Optimized scalar updates) ─────────────────► CountMinSketch
│
├── Heavy Hitters (Do you need to find the Top-K most frequent items?)
│   ├── Speed is paramount (Frequency decay algorithm) ──────────────────► HeavyKeeper
│   ├── Strongest theoretical bounds (O(1) updates and merges) ───────────► SpaceSaving
│   └── Simple implementation (Constant storage space) ──────────────────► MisraGries
│
├── Set Similarity (Do you need to estimate set overlaps or Jaccard metric?)
│   ├── Standard set intersection ──────────────────────────────────────► MinHash
│   ├── Frequencies/weighted elements ───────────────────────────────────► WeightedMinHash
│   ├── Low memory representation (k-bit signatures) ───────────────────► BBitMinHash
│   ├── Estimate size of set differences ────────────────────────────────► OddSketch
│   └── Near-duplicate document detection ──────────────────────────────► SimHash
│
├── Quantiles & Percentiles (Do you need medians, percentiles, or ranks?)
│   ├── Fixed relative error target, dense positive/negative range ─────► DDSketch
│   ├── Optimal theoretical space bounds, streaming merge ───────────────► KLLSketch
│   └── Centroid clustering, general-purpose production quantiles ────────► tDigest
│
├── Sliding Window (Do you need metrics over a temporal window?)
│   ├── Out-of-date element expiration ─────────────────────────────────► ExponentialHistogram
│   └── Approximate frequency counts in streaming windows ──────────────► LossyCounting
│
└── Streaming Selection (Do you need random representative samples?)
    └── Maintain a uniform sample from an infinite stream ───────────────► ReservoirSampler
```

---
## Quick start

### CMake Integration

Integrate probDS into your target executable in five lines using FetchContent:

```cmake
# CMakeLists.txt
include(FetchContent)
FetchContent_Declare(probds GIT_REPOSITORY https://github.com/username/probds.git GIT_TAG main)
FetchContent_MakeAvailable(probds)
target_link_libraries(your_target PRIVATE probds)
```

Install compiler dependencies via Homebrew on macOS:
```bash
brew install cmake gcc
```

### Basic Example: Set Membership
```cpp
#include "probds/bloom_filter.hpp"
#include <iostream>
#include <string_view>

int main() {
    probds::BloomFilter<std::string_view> filter(100000, 0.01);
    filter.insert("usr_session_9281");
    filter.insert("usr_session_1042");

    if (filter.possibly_contains("usr_session_9281")) {
        std::cout << "Key exists (1% false positive probability)" << std::endl;
    }
    std::cout << "Allocated memory: " << filter.memory_bytes() << " bytes" << std::endl;
    return 0;
}
```

### Basic Example: Cardinality Tracking
```cpp
#include "probds/hyperloglog.hpp"
#include <iostream>
#include <string_view>

int main() {
    probds::HyperLogLog<std::string_view> hll(14);
    hll.insert("ip_192_168_1_1");
    hll.insert("ip_10_0_0_1");
    hll.insert("ip_192_168_1_1");

    std::cout << "Estimated unique IPs: " << hll.estimate() << std::endl;
    return 0;
}
```

---
## Full benchmark table

probDS structures are benchmarked at three hardware cache scales:
- **L1 Cache Scale**: $N = 10^4$ ($10,000$ elements, fits entirely in L1 cache).
- **L3 Cache Scale**: $N = 10^6$ ($1,000,000$ elements, fits in L2/L3 cache boundary).
- **RAM Scale**: $N = 5 \times 10^7$ ($50,000,000$ elements, exceeds all processor caches, forcing DRAM accesses).

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

---
## Structure reference

### Set Membership Filters

#### BloomFilter
**Problem:** Checks set membership of stream elements in constant time and minimal space.
**Guarantee:** False positive rate is bounded by $FPR = (1 - e^{-kn/m})^k$, where $m$ is the bit array size, $n$ is the number of inserted elements, and $k$ is the number of hash functions.
**When to use over alternatives:** Use when insertion is dynamic and deletion is not required.
**Usage:**
```cpp
#include "probds/bloom_filter.hpp"
probds::BloomFilter<int> filter(10000, 0.01);
filter.insert(42);
bool exists = filter.possibly_contains(42);
```
**Benchmark highlight:** lookup (-) latency is 2.08 ns at L1 cache scale.

#### CuckooFilter
**Problem:** Dynamic set membership with support for element deletion.
**Guarantee:** Bounded false positive rate $\epsilon \le 2 \cdot 2^{-f}$ where $f$ is the fingerprint bit length, with a space load factor up to 95.5%.
**When to use over alternatives:** Use when dynamic deletion support is mandatory and high space load factors are required.
**Usage:**
```cpp
#include "probds/cuckoo_filter.hpp"
probds::CuckooFilter<int> filter(10000);
filter.insert(42);
bool exists = filter.possibly_contains(42);
filter.remove(42);
```
**Benchmark highlight:** lookup (+) latency is 1.76 ns at L1 cache scale.

#### CountingBloomFilter
**Problem:** Set membership filter that supports element deletion by maintaining integer counters.
**Guarantee:** Bounded false positive rate matching the standard Bloom filter, with counter overflow probability bounded by $P(c \ge 16) \le 1.37 \times 10^{-15} \times m$ using 4-bit counters.
**When to use over alternatives:** Use when deletion is required, but you prefer the simple array configuration of a Bloom filter over a Cuckoo filter.
**Usage:**
```cpp
#include "probds/counting_bloom_filter.hpp"
probds::CountingBloomFilter<int> filter(10000, 0.01);
filter.insert(42);
bool exists = filter.possibly_contains(42);
filter.remove(42);
```
**Benchmark highlight:** lookup (-) latency is 1.77 ns at L1 cache scale.

#### ScalableBloomFilter
**Problem:** Set membership filter that grows dynamically by stacking successive filters as the load increases.
**Guarantee:** Cumulative false positive rate is bounded by $FPR \le \sum_i P_i \le P_0 \frac{1}{1 - r}$, where $r$ is the scaling factor of false positive probabilities.
**When to use over alternatives:** Use when the total size of the streaming dataset is unknown at construction time.
**Usage:**
```cpp
#include "probds/scalable_bloom_filter.hpp"
probds::ScalableBloomFilter<int> filter(1000, 0.01);
filter.insert(42);
bool exists = filter.possibly_contains(42);
```
**Benchmark highlight:** insert latency is 7.85 ns at L1 cache scale.

#### BlockedBloomFilter
**Problem:** Set membership filter optimized for CPU cache lines using block-aligned structures.
**Guarantee:** Bounded false positive rate matching standard Bloom filter guarantees per block, with block occupancy modeled by a binomial distribution.
**When to use over alternatives:** Use when lookup throughput is the primary system bottleneck.
**Usage:**
```cpp
#include "probds/blocked_bloom_filter.hpp"
probds::BlockedBloomFilter<int> filter(10000, 0.01);
filter.insert(42);
bool exists = filter.possibly_contains(42);
```
**Benchmark highlight:** bulk SIMD lookup latency is 3.13 ns at L1 cache scale.

#### QuotientFilter
**Problem:** Cache-friendly set membership filter storing fingerprints in a compact, partitioned hash table.
**Guarantee:** Bounded false positive rate identical to a Cuckoo filter, but guarantees zero memory fragmentation due to bit-packed slot chains.
**When to use over alternatives:** Use when fingerprint deletion is required and cache misses must be minimized via contiguous storage.
**Usage:**
```cpp
#include "probds/quotient_filter.hpp"
probds::QuotientFilter<int> filter(16384);
filter.insert(42);
bool exists = filter.possibly_contains(42);
filter.remove(42);
```
**Benchmark highlight:** insert latency is 3.20 ns at L1 cache scale.

#### BinaryFuse8
**Problem:** Static set membership filter built over a complete dataset with extreme space efficiency.
**Guarantee:** Space overhead is bounded at 1.13 times the fingerprint size ($m \approx 1.13 \times n \times 8$ bits), with a false positive rate of $\epsilon \approx 0.39\%$.
**When to use over alternatives:** Use for static datasets when lookup latency and memory overhead are both critical.
**Usage:**
```cpp
#include "probds/binary_fuse8.hpp"
#include <vector>
std::vector<int> keys = {42, 100, 200};
probds::BinaryFuse8<int> filter(keys.begin(), keys.end());
bool exists = filter.possibly_contains(42);
```
**Benchmark highlight:** lookup (+) latency is 2.29 ns at L1 cache scale.

#### XorFilter
**Problem:** Static set membership filter solved via 3-way linear equations.
**Guarantee:** False positive rate of $\epsilon = 2^{-f}$ using $f$-bit fingerprints, with a space consumption of $1.23 \times n \times f$ bits.
**When to use over alternatives:** Use when the dataset is static and query latency must match Binary Fuse but construction speed is less critical.
**Usage:**
```cpp
#include "probds/xor_filter.hpp"
#include <vector>
std::vector<int> keys = {42, 100, 200};
probds::XorFilter<int> filter(keys.begin(), keys.end());
bool exists = filter.possibly_contains(42);
```
**Benchmark highlight:** lookup (+) latency is 2.31 ns at L1 cache scale.

#### RibbonFilter
**Problem:** Memory-efficient static membership filter that optimizes memory layout via solver band-matrices.
**Guarantee:** Achieves a space overhead arbitrary close to 0%, requiring only $\approx 1.02 \times n \times f$ bits with a false positive rate of $\epsilon = 2^{-f}$.
**When to use over alternatives:** Use when memory overhead is the primary bottleneck and the set is static.
**Usage:**
```cpp
#include "probds/ribbon_filter.hpp"
#include <vector>
std::vector<int> keys = {42, 100, 200};
probds::RibbonFilter<int> filter(keys.begin(), keys.end());
bool exists = filter.possibly_contains(42);
```
**Benchmark highlight:** lookup (+) latency is 4.73 ns at L1 cache scale.

#### LearnedBloomFilter
**Problem:** Set membership filter that leverages a machine learning model as a pre-filter.
**Guarantee:** Optimizes the standard false positive rate to $FPR_{LBF} = FPR_{backup} \times (1 - P(Classify_{correct}))$.
**When to use over alternatives:** Use when data is highly structured and a classifier model is available.
**Usage:**
```cpp
#include "probds/learned_bloom_filter.hpp"
#include "probds/bloom_filter.hpp"
struct MockClassifier {
    double operator()(int key) const { return key == 42 ? 0.99 : 0.01; }
};
probds::BloomFilter<int> backup(1000, 0.01);
probds::LearnedBloomFilter<int, MockClassifier> filter(MockClassifier{}, backup);
bool exists = filter.possibly_contains(42);
```
**Benchmark highlight:** insert latency is 2.85 ns at L1 cache scale.

### Cardinality & Frequency Sketches

#### HyperLogLog
**Problem:** Estimates the number of unique elements in a streaming dataset.
**Guarantee:** The relative error is bounded by $1.04 / \sqrt{m}$, where $m = 2^p$ is the number of registers.
**When to use over alternatives:** Use when unique counts must be tracked for extremely high cardinality streams within a tiny, constant memory footprint.
**Usage:**
```cpp
#include "probds/hyperloglog.hpp"
probds::HyperLogLog<int> hll(14);
hll.insert(42);
std::uint64_t count = hll.estimate();
```
**Benchmark highlight:** insert latency is 1.23 ns at L1 cache scale.

#### CountMinSketch
**Problem:** Estimates the frequencies of elements in a streaming dataset.
**Guarantee:** The frequency estimate $\hat{f}_x$ satisfies $f_x \le \hat{f}_x \le f_x + \epsilon N$ with probability at least $1 - \delta$, where $N$ is the total number of inserted items.
**When to use over alternatives:** Use when frequency counts must be tracked and systematic overestimation (no underestimation) is acceptable.
**Usage:**
```cpp
#include "probds/count_min_sketch.hpp"
probds::CountMinSketch<int> cms(0.01, 0.01);
cms.insert(42, 10);
std::uint64_t freq = cms.estimate(42);
```
**Benchmark highlight:** insert latency is 4.59 ns at L1 cache scale.

#### CountMinLog
**Problem:** Logarithmic variant of Count-Min Sketch that compresses counters.
**Guarantee:** Bounded error guarantees matching Count-Min Sketch but uses small logarithmic registers (e.g., 4-bit counters using Morris' algorithm base), achieving up to 15x memory reduction.
**When to use over alternatives:** Use when frequency estimation memory must be minimized and high scale makes standard counters too large.
**Usage:**
```cpp
#include "probds/count_min_log.hpp"
probds::CountMinLog<int> cml(0.01, 0.01);
cml.insert(42, 10);
std::uint64_t freq = cml.estimate(42);
```
**Benchmark highlight:** insert latency is 7.97 ns at L1 cache scale.

#### CountSketch
**Problem:** Estimates element frequencies in a stream with unbiased errors.
**Guarantee:** Unbiased frequency estimate $\mathbb{E}[\hat{f}_x] = f_x$ with variance bounded by $\text{Var}(\hat{f}_x) \le F_2 / w$, where $F_2 = \sum_y f_y^2$ is the second frequency moment.
**When to use over alternatives:** Use when unbiased estimates (errors can be positive or negative) are required.
**Usage:**
```cpp
#include "probds/count_sketch.hpp"
probds::CountSketch<int> cs(0.01, 0.01);
cs.insert(42, 10);
std::int64_t freq = cs.estimate(42);
```
**Benchmark highlight:** insert latency is 9.71 ns at L1 cache scale.

#### AMSSketch
**Problem:** Estimates the second frequency moment ($F_2$ self-join size) of a stream.
**Guarantee:** Bounded approximation error $(1 \pm \epsilon) F_2$ with probability at least $1 - \delta$.
**When to use over alternatives:** Use when the self-join size or variance of the stream distribution must be estimated.
**Usage:**
```cpp
#include "probds/ams_sketch.hpp"
probds::AMSSketch<int> ams(0.01, 0.01);
ams.insert(42, 1);
double f2 = ams.estimate();
```
**Benchmark highlight:** insert latency is 3.74 ns at L1 cache scale.

#### ExponentialHistogram
**Problem:** Counts element frequencies over a sliding window with temporal decay.
**Guarantee:** Bounded relative error $\epsilon$ for window count queries, storing counts in exponentially growing buckets.
**When to use over alternatives:** Use when element frequencies must be tracked exclusively over a recent time window.
**Usage:**
```cpp
#include "probds/exponential_histogram.hpp"
probds::ExponentialHistogram eh(0.01);
eh.update(1); // timestamp=1
double count = eh.estimate(10); // count within window size 10
```
**Benchmark highlight:** estimate latency is 55.16 ns at L1 cache scale.

### Quantile Estimators

#### tDigest
**Problem:** Estimates quantiles (percentiles, medians) of streaming values.
**Guarantee:** Extreme accuracy at the tails (near 0 and 1) using dynamically clustered centroids, scaling with compression factor $q$.
**When to use over alternatives:** Use when high-accuracy quantile estimates are needed, especially at the extremes (e.g. 99.9th percentile).
**Usage:**
```cpp
#include "probds/tdigest.hpp"
probds::tDigest td(100.0);
td.insert(1.5);
double p50 = td.get_quantile(0.5);
```
**Benchmark highlight:** insert latency is 12.54 ns at L1 cache scale.

#### KLLSketch
**Problem:** Estimates streaming quantiles with space-optimal guarantees.
**Guarantee:** The absolute rank error is bounded by $\epsilon$ with high probability, requiring only $O((1/\epsilon) \log \log(1/\epsilon))$ memory.
**When to use over alternatives:** Use when strong space-optimal theoretical bounds are required.
**Usage:**
```cpp
#include "probds/kll_sketch.hpp"
probds::KLLSketch kll(200);
kll.insert(1.5);
double median = kll.get_quantile(0.5);
```
**Benchmark highlight:** insert latency is 19.72 ns at L1 cache scale.

#### DDSketch
**Problem:** Computes quantiles of streaming values with a guaranteed relative error.
**Guarantee:** The estimated quantile $\hat{q}$ satisfies $(1 - \alpha) q \le \hat{q} \le (1 + \alpha) q$ for relative error target $\alpha$.
**When to use over alternatives:** Use when relative-error guarantees (e.g. 1% error of the actual value) are required across the entire value range.
**Usage:**
```cpp
#include "probds/dd_sketch.hpp"
probds::DDSketch dd(0.01);
dd.insert(100.0);
double val = dd.get_quantile(0.55);
```
**Benchmark highlight:** insert latency is 4.74 ns at L1 cache scale.

### Similarity, Sampling & Heavy Hitters

#### MinHash
**Problem:** Estimates the Jaccard similarity of two large datasets.
**Guarantee:** Unbiased estimator of the Jaccard similarity $J(A, B) = |A \cap B| / |A \cup B|$.
**When to use over alternatives:** Use when you need to compute overlap ratios between sets without storing their contents.
**Usage:**
```cpp
#include "probds/minhash.hpp"
probds::MinHash<int> mh1(128), mh2(128);
mh1.insert(42); mh2.insert(42);
double similarity = mh1.jaccard_similarity(mh2);
```
**Benchmark highlight:** Jaccard query latency is 16.61 ns for $k=128$.

#### WeightedMinHash
**Problem:** Estimates the Jaccard similarity for multi-sets or weighted datasets.
**Guarantee:** Unbiased estimator of the weighted Jaccard similarity $J_W(x, y) = \sum_i \min(x_i, y_i) / \sum_i \max(x_i, y_i)$.
**When to use over alternatives:** Use when set elements have frequencies or real-valued weights.
**Usage:**
```cpp
#include "probds/weighted_minhash.hpp"
probds::WeightedMinHash<int> wmh1(128), wmh2(128);
wmh1.insert(42, 5.0); wmh2.insert(42, 3.0);
double similarity = wmh1.jaccard_similarity(wmh2);
```
**Benchmark highlight:** Jaccard query latency is 62.35 ns for $k=128$.

#### BBitMinHash
**Problem:** Highly compressed MinHash signatures using only the lowest $b$ bits.
**Guarantee:** Estimates Jaccard similarity with bounded variance using only $b$ bits per hash (typically 1 or 2 bits).
**When to use over alternatives:** Use when network or storage footprint of similarity signatures is the primary bottleneck.
**Usage:**
```cpp
#include "probds/bbit_minhash.hpp"
probds::BBitMinHash<int> bbm1(128), bbm2(128);
bbm1.insert(42); bbm2.insert(42);
double similarity = bbm1.jaccard_similarity(bbm2);
```
**Benchmark highlight:** Jaccard query latency is 56.87 ns for $k=128$.

#### OddSketch
**Problem:** Estimates the size of symmetric differences and Jaccard similarity between sets.
**Guarantee:** Estimates symmetric difference size $|A \Delta B|$ directly from bit flips.
**When to use over alternatives:** Use when Jaccard metrics are needed, but you also need to estimate the exact count of different items.
**Usage:**
```cpp
#include "probds/odd_sketch.hpp"
probds::OddSketch<int> os1(1024), os2(1024);
os1.insert(42); os2.insert(43);
double distance = os1.symmetric_difference(os2);
```
**Benchmark highlight:** insert latency is 0.75 ns at L1 cache scale.

#### SimHash
**Problem:** Computes locality-sensitive signatures to detect near-duplicate documents.
**Guarantee:** The Hamming distance between signatures is proportional to the cosine distance between the document vectors.
**When to use over alternatives:** Use for document deduping or vector cosine similarity.
**Usage:**
```cpp
#include "probds/simhash.hpp"
probds::SimHash<int> sh(64);
sh.insert(42, 1.0);
std::uint64_t sig = sh.get_fingerprint();
```
**Benchmark highlight:** insert latency is 9.83 ns at L1 cache scale.

#### MisraGries
**Problem:** Identifies heavy hitters (elements occurring more than $N/k$ times).
**Guarantee:** Guarantees that any item exceeding the frequency threshold is retained in the sketch.
**When to use over alternatives:** Use when a simple associative array structure with a hard frequency bounds guarantee is preferred.
**Usage:**
```cpp
#include "probds/misra_gries.hpp"
probds::MisraGries<int> mg(100);
mg.insert(42);
```
**Benchmark highlight:** insert latency is 32.23 ns at L1 cache scale.

#### SpaceSaving
**Problem:** Identifies heavy hitters in a stream with $O(1)$ operations.
**Guarantee:** Frequency estimates for retained elements have an error of at most $N/k$.
**When to use over alternatives:** Use when strong guarantees on heavy hitter identification must be combined with $O(1)$ updates and mergers.
**Usage:**
```cpp
#include "probds/space_saving.hpp"
probds::SpaceSaving<int> ss(100);
ss.insert(42);
```
**Benchmark highlight:** insert latency is 43.91 ns at L1 cache scale.

#### HeavyKeeper
**Problem:** Finds the Top-K heaviest items in a Zipfian stream.
**Guarantee:** Employs exponential decay algorithm to evict low-frequency items from a 2D bucket array, providing high precision.
**When to use over alternatives:** Use on skewed streams (Zipfian) to find the heavy hitters with minimal false trackings.
**Usage:**
```cpp
#include "probds/heavy_keeper.hpp"
probds::HeavyKeeper<int> hk(10, 4, 1024);
hk.insert(42);
```
**Benchmark highlight:** insert latency is 12.14 ns at L1 cache scale.

#### ReservoirSampler
**Problem:** Selects a uniform random sample of size $k$ from an infinite stream.
**Guarantee:** Every element seen in the stream has an equal probability $k / N$ of being retained in the reservoir.
**When to use over alternatives:** Use when you need to maintain a representative sample from a stream of unknown length.
**Usage:**
```cpp
#include "probds/reservoir_sampler.hpp"
probds::ReservoirSampler<int> rs(100);
rs.insert(42);
```
**Benchmark highlight:** insert latency is 4.10 ns at L1 cache scale.

#### LossyCounting
**Problem:** Frequency tracker and heavy hitter detector over streaming windows.
**Guarantee:** Bounded relative error values matching the standard heavy hitter criteria.
**When to use over alternatives:** Use when you want a simple epoch-based frequency tracker.
**Usage:**
```cpp
#include "probds/lossy_counting.hpp"
probds::LossyCounting<int> lc(0.001);
lc.insert(42);
```
**Benchmark highlight:** insert latency is 30.00 ns at L1 cache scale.
### Concurrent Variants

#### ConcurrentBloomFilter
**Problem:** Thread-safe dynamic set membership filter.
**Guarantee:** Bounded false positive rate matching the standard Bloom filter, safe for concurrent readers and writers.
**When to use over alternatives:** Use when multiple threads must concurrently insert or query a set membership filter.
**Usage:**
```cpp
#include "probds/concurrent.hpp"
probds::ConcurrentBloomFilter<int> filter(10000, 0.01, 128);
filter.insert(42);
bool exists = filter.possibly_contains(42);
```
**Benchmark highlight:** insert latency is 12.00 ns (1 thread) / 24.29 ns (8 threads).

#### ConcurrentHyperLogLog
**Problem:** Thread-safe cardinality estimator.
**Guarantee:** Cardinality estimation matching standard HyperLogLog error bounds with thread-safe atomic updates.
**When to use over alternatives:** Use when multiple worker threads process a stream and must update a single cardinality sketch.
**Usage:**
```cpp
#include "probds/concurrent.hpp"
probds::ConcurrentHyperLogLog<int> hll(14, 64);
hll.insert(42);
std::uint64_t count = hll.estimate();
```
**Benchmark highlight:** insert latency is 4.49 ns (1 thread) / 8.02 ns (8 threads).

#### ConcurrentCountMin
**Problem:** Thread-safe frequency sketch.
**Guarantee:** Frequency estimation matching standard Count-Min Sketch error bounds, built on atomic register cells with relaxed memory ordering.
**When to use over alternatives:** Use when multiple threads are tracking element frequencies concurrently.
**Usage:**
```cpp
#include "probds/concurrent.hpp"
probds::ConcurrentCountMin<int> ccm(0.01, 0.01);
ccm.insert(42, 10);
std::int64_t freq = ccm.estimate(42);
```
**Benchmark highlight:** insert latency is 7.35 ns (1 thread) / 25.56 ns (8 threads).

---
## Mathematical foundations

### Bloom Filter Family

A Bloom filter represents a set $S = \{x_1, x_2, \dots, x_n\}$ using a bit array of size $m$, initialized to 0. It utilizes $k$ independent hash functions $h_1, h_2, \dots, h_k$ mapping elements to $[0, m-1]$. To insert $x \in S$, the bits at indices $h_i(x)$ are set to 1 for all $1 \le i \le k$. To query $y$, we verify if all bits at $h_i(y)$ are 1; if any bit is 0, $y$ is definitively not in $S$.

The probability that a specific bit is not set to 1 by any of the $k$ hash functions during $n$ insertions is $(1 - 1/m)^{kn} \approx e^{-kn/m}$. The probability of a false positive (that all $k$ bits are 1 for an element not in the set) is therefore:
$$FPR = \left(1 - \left(1 - \frac{1}{m}\right)^{kn}\right)^k \approx \left(1 - e^{-kn/m}\right)^k$$
To find the optimal number of hash functions $k$ that minimizes this expression for a given ratio $m/n$, we differentiate with respect to $k$, yielding:
$$k = \frac{m}{n} \ln 2$$
At this optimum, the false positive probability simplifies to $2^{-k} \approx (0.6185)^{m/n}$. To avoid evaluating $k$ independent hash functions, probDS implements the Kirsch-Mitzenmacher optimization, which derives $k$ indices using only two hash values $h_1(x)$ and $h_2(x)$ via the formula $g_i(x) = (h_1(x) + i \cdot h_2(x)) \pmod m$. This technique retains the asymptotic false positive guarantees of standard independent hashing.

### HyperLogLog Family

HyperLogLog estimates the cardinality of a multiset by analyzing the distribution of the largest number of leading zeros in the hash values of the elements. Assuming a hash function maps keys uniformly to the binary domain $\{0, 1\}^{64}$, the probability of observing a hash starting with $r$ consecutive zeros is $2^{-(r+1)}$. Thus, observing a hash with $r$ leading zeros indicates that the cardinality of the set is likely on the order of $2^r$.

To reduce the high variance of this single-estimator model, HyperLogLog partitions the stream into $m = 2^p$ substreams using the first $p$ bits of each hash value. The remaining bits are used to compute the leading zero count, which is stored in a register array $M$. The estimator calculates the harmonic mean of the register values to suppress the influence of outliers (which occur when an element randomly hashes to a very large number of leading zeros):
$$E = \alpha_m m^2 \left( \sum_{j=1}^m 2^{-M[j]} \right)^{-1}$$
where $\alpha_m$ is a constant bias-correction factor (e.g., $\alpha_m \approx 0.7213$ for large $m$). The harmonic mean ensures that the relative error is bounded tightly around $1.04 / \sqrt{m}$. When the estimation $E$ is small, the registers contain many zeros; HyperLogLog corrects this non-linear compression using Linear Counting:
$$E = m \ln\left(\frac{m}{V}\right)$$
where $V$ is the number of registers containing 0.

### Count-Min Family

The Count-Min Sketch is a 2D array of width $w$ and depth $d$. It utilizes $d$ independent hash functions $h_1, \dots, h_d$, where each $h_j$ maps elements to $[0, w-1]$. When an element $x$ is inserted with count $c$, the sketch increments the cell at row $j$ and column $h_j(x)$ by $c$:
$$\text{table}[j][h_j(x)] \leftarrow \text{table}[j][h_j(x)] + c$$
To estimate the frequency of $x$, the sketch returns the minimum value across all mapped cells:
$$\hat{f}_x = \min_{1 \le j \le d} \text{table}[j][h_j(x)]$$
Because the cells only increment and hashes can collide, the estimate is guaranteed to never underestimate the true frequency ($f_x \le \hat{f}_x$). The overestimate error is bounded by:
$$\hat{f}_x \le f_x + \epsilon \|a\|_1$$
where $\|a\|_1 = N$ is the total sum of all inserted stream frequencies. By selecting $w = \lceil e / \epsilon \rceil$ and $d = \lceil \ln(1/\delta) \rceil$, the error is restricted to $\epsilon N$ with confidence at least $1 - \delta$. The Count-Min Log sketch compresses this footprint by storing logarithmic counters (Morris counters), representing register values as exponent approximations. When a register value is $v$, the probability of incrementing it during an insertion is set to $2^{-v}$, reducing the register size to 4 bits while preserving statistical error bounds.

### Similarity Sketches

The MinHash sketch estimates the Jaccard similarity between two sets $A$ and $B$, defined as $J(A, B) = |A \cap B| / |A \cup B|$. MinHash utilizes $k$ independent, random, permutation-style hash functions $h_1, \dots, h_k$. For a set $A$, the sketch stores a signature vector $S(A)$ consisting of the minimum hash values achieved by any element in $A$ for each hash function:
$$S(A)_i = \min_{x \in A} h_i(x)$$
For a single hash function $h$, the probability that the minimum hash value of $A \cup B$ belongs to an element in $A \cap B$ is exactly the ratio of their sizes:
$$P\left(\min_{x \in A} h(x) = \min_{y \in B} h(y)\right) = \frac{|A \cap B|}{|A \cup B|} = J(A, B)$$
Thus, the fraction of matching entries in the signature vectors $S(A)$ and $S(B)$ is an unbiased estimator of $J(A, B)$. This permits comparing massive datasets (e.g. web pages) by evaluating Hamming matches over small integer arrays. In Locality-Sensitive Hashing (LSH), these signatures are partitioned into bands to index near-duplicate items into common buckets in sub-linear time.

---
## Building and testing

### Clone and build
```bash
git clone https://github.com/username/probds
cd probds
cmake -B build -DPROBDS_BUILD_TESTS=ON -DPROBDS_BUILD_BENCHMARKS=ON
cmake --build build -j$(nproc)

# Run tests
cd build && ctest --output-on-failure

# Run benchmarks
./build/probds_benchmarks --benchmark_format=console
```

Every structure in probDS undergoes rigorous unit testing using the Google Test framework.

---

## Adding a new structure

Follow this checklist to contribute a new structure to probDS:

1. **Implementation**: Create a header file under `include/probds/your_structure.hpp`. It must be fully header-only, templated on the key type `T` and the `Hash` policy, and allocate zero heap memory inside query/lookup operations.
2. **Tests**: Add a unit test file under `tests/your_structure_test.cpp`. The test suite must verify the structure's empirical error rates against theoretical bounds at four scales ($10^3, 10^4, 10^5, 10^6$ elements), and verify serialization round-trips.
3. **Benchmarks**: Add a benchmark suite in `benchmarks/your_structure_bench.cpp` using Google Benchmark. Sizing parameters must follow the three-scale format (L1/L3/RAM) and compare metrics against the equivalent naive exact C++ container.
4. **CMake Integration**: Add the test and benchmark sources to `CMakeLists.txt`.
5. **Documentation**: Update this `README.md` file with a new entry in the Structure Reference section and append the benchmark metrics to the main table.

---

## License

MIT