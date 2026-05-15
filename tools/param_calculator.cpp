// =============================================================================
// param_calculator.cpp — Parameter Sizing Calculator CLI for probDS
// =============================================================================

#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <algorithm>

static std::size_t next_pow2(std::size_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    n++;
    return n;
}

static void print_usage() {
    std::cout << "Usage: param_calculator --structure <type> [options]\n"
              << "Structures:\n"
              << "  bloom           Bloom Filter\n"
              << "  cuckoo          Cuckoo Filter\n"
              << "  hll             HyperLogLog\n"
              << "  cms             Count-Min Sketch\n"
              << "  counting_bloom  Counting Bloom Filter\n"
              << "  blocked_bloom   Blocked Bloom Filter\n"
              << "  quotient        Quotient Filter\n"
              << "  binary_fuse8    Binary Fuse Filter (8-bit)\n"
              << "  heavy_keeper    HeavyKeeper Sketch\n"
              << "  dd_sketch       DDSketch relative-error quantiles\n"
              << "Options:\n"
              << "  --n <num>             Expected insertions (required for filters/CMS/HeavyKeeper)\n"
              << "  --fpr <rate>          Target false positive rate (default: 0.01)\n"
              << "  --error <err>         Target error rate (default: 0.01 for HLL/CMS)\n"
              << "  --confidence <conf>   Target confidence rate (default: 0.99 for CMS)\n"
              << "  --memory <bytes>      Optionally find optimal params under a memory budget (in bytes)\n"
              << "  --d <depth>           HeavyKeeper depth parameter (default: 4)\n"
              << "  --w <width>           HeavyKeeper width parameter (default: 1024)\n"
              << "  --k <top_k>           HeavyKeeper Top-K parameter (default: 100)\n";
}

int main(int argc, char* argv[]) {
    std::string structure = "";
    double n = -1;
    double fpr = 0.01;
    double error = 0.01;
    double confidence = 0.99;
    double budget = -1;
    double d_param = 4;
    double w_param = 1024;
    double k_param = 100;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--structure" && i + 1 < argc) {
            structure = argv[++i];
        } else if (arg == "--n" && i + 1 < argc) {
            n = std::stod(argv[++i]);
        } else if (arg == "--fpr" && i + 1 < argc) {
            fpr = std::stod(argv[++i]);
        } else if (arg == "--error" && i + 1 < argc) {
            error = std::stod(argv[++i]);
        } else if (arg == "--confidence" && i + 1 < argc) {
            confidence = std::stod(argv[++i]);
        } else if (arg == "--memory" && i + 1 < argc) {
            budget = std::stod(argv[++i]);
        } else if (arg == "--d" && i + 1 < argc) {
            d_param = std::stod(argv[++i]);
        } else if (arg == "--w" && i + 1 < argc) {
            w_param = std::stod(argv[++i]);
        } else if (arg == "--k" && i + 1 < argc) {
            k_param = std::stod(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        }
    }

    if (structure.empty()) {
        std::cerr << "Error: --structure is required.\n";
        print_usage();
        return 1;
    }

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "========================================================\n";
    std::cout << "           probDS Parameter Sizing Calculator           \n";
    std::cout << "========================================================\n";
    std::cout << "Structure: " << structure << "\n";

    if (structure == "bloom" || structure == "blocked_bloom") {
        if (n <= 0) {
            std::cerr << "Error: --n (expected insertions) > 0 is required for Bloom Filters.\n";
            return 1;
        }
        std::cout << "Expected Insertions (n): " << static_cast<std::size_t>(n) << "\n";
        std::cout << "Target FPR: " << fpr * 100.0 << "%\n";

        double ln2 = std::log(2.0);
        double m_opt = -n * std::log(fpr) / (ln2 * ln2);
        std::size_t m_rounded = next_pow2(static_cast<std::size_t>(std::ceil(m_opt)));
        if (m_rounded < 64) m_rounded = 64;

        std::size_t k = static_cast<std::size_t>(std::round(static_cast<double>(m_rounded) / n * ln2));
        if (k == 0) k = 1;

        double actual_fpr = std::pow(1.0 - std::exp(-static_cast<double>(k) * n / m_rounded), k);

        std::cout << "\nCalculated Parameters:\n";
        std::cout << "  Optimal Bit Count (m): " << m_rounded << " (rounded up to next power of 2)\n";
        std::cout << "  Optimal Hash Count (k): " << k << "\n";
        std::cout << "  Theoretical Actual FPR: " << actual_fpr * 100.0 << "%\n";
        std::cout << "  Total Memory Required: " << m_rounded / 8 << " bytes (" << (double)m_rounded / 8 / 1024 << " KB)\n";

    } else if (structure == "counting_bloom") {
        if (n <= 0) {
            std::cerr << "Error: --n (expected insertions) > 0 is required.\n";
            return 1;
        }
        std::cout << "Expected Insertions (n): " << static_cast<std::size_t>(n) << "\n";
        std::cout << "Target FPR: " << fpr * 100.0 << "%\n";

        double ln2 = std::log(2.0);
        double m_opt = -n * std::log(fpr) / (ln2 * ln2);
        std::size_t m_rounded = next_pow2(static_cast<std::size_t>(std::ceil(m_opt)));
        if (m_rounded < 64) m_rounded = 64;

        std::size_t k = static_cast<std::size_t>(std::round(static_cast<double>(m_rounded) / n * ln2));
        if (k == 0) k = 1;

        std::cout << "\nCalculated Parameters:\n";
        std::cout << "  Optimal Counter Count (m): " << m_rounded << " (rounded up to next power of 2)\n";
        std::cout << "  Optimal Hash Count (k): " << k << "\n";
        std::cout << "  Total Memory Required: " << m_rounded << " bytes (8-bit counters) (" << (double)m_rounded / 1024 << " KB)\n";

    } else if (structure == "cuckoo") {
        if (n <= 0) {
            std::cerr << "Error: --n (expected insertions) > 0 is required.\n";
            return 1;
        }
        std::cout << "Expected Insertions (n): " << static_cast<std::size_t>(n) << "\n";
        std::cout << "Target FPR: " << fpr * 100.0 << "%\n";

        // 4 slots per bucket, load factor alpha = 0.955
        std::size_t slots = 4;
        double alpha = 0.955;
        
        // Fingerprint size f = ceil(log2(2 * slots / fpr)) = ceil(log2(8 / fpr))
        std::size_t f = static_cast<std::size_t>(std::ceil(std::log2(8.0 / fpr)));
        
        std::size_t min_buckets = static_cast<std::size_t>(std::ceil(n / (slots * alpha)));
        std::size_t num_buckets = next_pow2(min_buckets);

        std::size_t total_bits = num_buckets * slots * f;

        std::cout << "\nCalculated Parameters:\n";
        std::cout << "  Bucket Count: " << num_buckets << " (rounded to power of 2)\n";
        std::cout << "  Slots per Bucket: " << slots << "\n";
        std::cout << "  Fingerprint Bits (f): " << f << " bits\n";
        std::cout << "  Total Memory Required: " << total_bits / 8 << " bytes (" << (double)total_bits / 8 / 1024 << " KB)\n";

    } else if (structure == "hll") {
        std::cout << "Target Relative Error: " << error * 100.0 << "%\n";

        // relative error = 1.04 / sqrt(m)
        double m_opt = (1.04 / error) * (1.04 / error);
        std::size_t m_rounded = next_pow2(static_cast<std::size_t>(std::ceil(m_opt)));
        if (m_rounded < 16) m_rounded = 16;
        if (m_rounded > 65536) m_rounded = 65536;

        std::size_t p = static_cast<std::size_t>(std::log2(m_rounded));
        double actual_error = 1.04 / std::sqrt(m_rounded);

        std::cout << "\nCalculated Parameters:\n";
        std::cout << "  Precision parameter (p): " << p << "\n";
        std::cout << "  Register Count (m): " << m_rounded << "\n";
        std::cout << "  Theoretical Actual Error: " << actual_error * 100.0 << "%\n";
        std::cout << "  Total Memory Required: " << m_rounded << " bytes (1 byte/register) (" << (double)m_rounded / 1024 << " KB)\n";

    } else if (structure == "cms") {
        std::cout << "Target Error Fraction (epsilon): " << error << "\n";
        std::cout << "Target Confidence (1 - delta): " << confidence * 100.0 << "%\n";

        // w = ceil(e / epsilon)
        double e = std::exp(1.0);
        std::size_t w_opt = static_cast<std::size_t>(std::ceil(e / error));
        std::size_t w = next_pow2(w_opt);

        // d = ceil(ln(1 / delta))
        double delta = 1.0 - confidence;
        std::size_t d = static_cast<std::size_t>(std::ceil(std::log(1.0 / delta)));

        std::size_t total_cells = w * d;
        std::size_t total_bytes = total_cells * 4; // 32-bit integer counters

        std::cout << "\nCalculated Parameters:\n";
        std::cout << "  Width of sketch (w): " << w << " (rounded to next power of 2)\n";
        std::cout << "  Depth of sketch (d): " << d << "\n";
        std::cout << "  Total counters: " << total_cells << "\n";
        std::cout << "  Total Memory Required: " << total_bytes << " bytes (" << (double)total_bytes / 1024 << " KB)\n";

    } else if (structure == "quotient") {
        if (n <= 0) {
            std::cerr << "Error: --n (expected insertions) > 0 is required.\n";
            return 1;
        }
        std::cout << "Expected Insertions (n): " << static_cast<std::size_t>(n) << "\n";
        std::cout << "Target FPR: " << fpr * 100.0 << "%\n";

        // Load factor alpha = 0.75
        double alpha = 0.75;
        std::size_t slots_opt = static_cast<std::size_t>(std::ceil(n / alpha));
        std::size_t slots = next_pow2(slots_opt);
        std::size_t q = static_cast<std::size_t>(std::log2(slots));

        // FPR approx 2^-r => r = ceil(-log2(fpr))
        std::size_t r = static_cast<std::size_t>(std::ceil(-std::log2(fpr)));

        // Slot holds r-bit remainder + 3 status bits
        std::size_t slot_bits = r + 3;
        std::size_t total_bits = slots * slot_bits;

        std::cout << "\nCalculated Parameters:\n";
        std::cout << "  Quotient Bits (q): " << q << " (table size: " << slots << " slots)\n";
        std::cout << "  Remainder Bits (r): " << r << "\n";
        std::cout << "  Bits per Slot: " << slot_bits << " bits\n";
        std::cout << "  Total Memory Required: " << total_bits / 8 << " bytes (" << (double)total_bits / 8 / 1024 << " KB)\n";

    } else if (structure == "binary_fuse8") {
        if (n <= 0) {
            std::cerr << "Error: --n (expected insertions) > 0 is required.\n";
            return 1;
        }
        std::cout << "Expected Insertions (n): " << static_cast<std::size_t>(n) << "\n";
        std::cout << "Target FPR: ~0.39% (Fixed for 8-bit fingerprint)\n";

        std::size_t segment_len = 1 << static_cast<unsigned>(std::floor(std::log(static_cast<double>(n)) / std::log(3.33) + 2.25));
        if (segment_len > 262144) {
            segment_len = 262144;
        }
        double sizeFactor = n <= 1 ? 0 : std::max(1.125, 0.875 + 0.25 * std::log(1000000.0) / std::log(static_cast<double>(n)));
        std::size_t capacity = n <= 1 ? 0 : static_cast<std::size_t>(std::round(static_cast<double>(n) * sizeFactor));
        std::size_t initSegmentCount = (capacity + segment_len - 1) / segment_len - 2;
        std::size_t array_len = (initSegmentCount + 2) * segment_len;
        std::size_t total_bytes = array_len * sizeof(std::uint8_t);

        std::cout << "\nCalculated Parameters:\n";
        std::cout << "  Segment Length: " << segment_len << "\n";
        std::cout << "  Total Filter Slots (Array Length): " << array_len << "\n";
        std::cout << "  Arity (Hash Count): 3 (Fixed)\n";
        std::cout << "  Total Memory Required: " << total_bytes << " bytes (" << (double)total_bytes / 1024 << " KB)\n";

    } else if (structure == "heavy_keeper") {
        std::size_t table_bytes = static_cast<std::size_t>(d_param) * static_cast<std::size_t>(w_param) * 8;
        std::size_t summary_bytes = static_cast<std::size_t>(k_param) * 128;
        std::size_t total_bytes = table_bytes + summary_bytes;

        std::cout << "\nCalculated Parameters:\n";
        std::cout << "  Depth (d): " << static_cast<std::size_t>(d_param) << "\n";
        std::cout << "  Width (w): " << static_cast<std::size_t>(w_param) << "\n";
        std::cout << "  Top-K (k): " << static_cast<std::size_t>(k_param) << "\n";
        std::cout << "  Table Memory Required: " << table_bytes << " bytes (" << (double)table_bytes / 1024 << " KB)\n";
        std::cout << "  Stream-Summary Est. Memory: " << summary_bytes << " bytes (" << (double)summary_bytes / 1024 << " KB)\n";
        std::cout << "  Total Est. Memory Required: " << total_bytes << " bytes (" << (double)total_bytes / 1024 << " KB)\n";

    } else if (structure == "dd_sketch") {
        std::cout << "Target Relative Error (alpha): " << error << "\n";
        double g = (1.0 + error) / (1.0 - error);
        double lng = std::log(g);
        std::size_t est_buckets = static_cast<std::size_t>(std::ceil(std::log(1e5) / lng));
        std::size_t est_bytes = est_buckets * 8 + 64; // sizeof DDSketch approximation

        std::cout << "\nCalculated Parameters:\n";
        std::cout << "  Gamma (base): " << g << "\n";
        std::cout << "  Estimated Buckets (for 5-decade range): " << est_buckets << "\n";
        std::cout << "  Estimated Memory Required: " << est_bytes << " bytes (" << (double)est_bytes / 1024 << " KB)\n";

    } else {
        std::cerr << "Error: unknown structure type '" << structure << "'\n";
        return 1;
    }

    if (budget > 0) {
        std::cout << "\n========================================================\n";
        std::cout << "Memory Budget Analysis:\n";
        std::cout << "  Allowed Budget: " << static_cast<std::size_t>(budget) << " bytes\n";
        // Simple sizing suggestion
        if (structure == "bloom") {
            std::size_t bits_allowed = static_cast<std::size_t>(budget) * 8;
            std::size_t m_pow2 = 1;
            while (m_pow2 * 2 <= bits_allowed) {
                m_pow2 *= 2;
            }
            std::size_t suggested_n = static_cast<std::size_t>(m_pow2 * std::log(2.0) * std::log(2.0) / -std::log(fpr));
            std::cout << "  Suggested Bloom Filter configuration within budget:\n"
                      << "    Max bits: " << m_pow2 << " (" << m_pow2 / 8 << " bytes)\n"
                      << "    Safe capacity (n) at " << fpr * 100.0 << "% FPR: " << suggested_n << " elements\n";
        } else if (structure == "hll") {
            std::size_t m_pow2 = 16;
            while (m_pow2 * 2 <= budget) {
                m_pow2 *= 2;
            }
            if (m_pow2 > 65536) m_pow2 = 65536;
            std::size_t p = static_cast<std::size_t>(std::log2(m_pow2));
            double actual_err = 1.04 / std::sqrt(m_pow2);
            std::cout << "  Suggested HyperLogLog configuration within budget:\n"
                      << "    Registers (m): " << m_pow2 << " (" << m_pow2 << " bytes)\n"
                      << "    Precision parameter (p): " << p << "\n"
                      << "    Expected error: " << actual_err * 100.0 << "%\n";
        } else if (structure == "binary_fuse8") {
            std::size_t capacity_allowed = budget;
            std::size_t suggested_n = static_cast<std::size_t>(capacity_allowed / 1.125);
            std::cout << "  Suggested Binary Fuse Filter configuration within budget:\n"
                      << "    Max capacity (n): ~" << suggested_n << " elements\n";
        } else if (structure == "heavy_keeper") {
            std::size_t summary_est = static_cast<std::size_t>(k_param) * 128;
            if (budget > summary_est) {
                std::size_t table_allowed = static_cast<std::size_t>(budget) - summary_est;
                std::size_t w_suggested = table_allowed / (8 * static_cast<std::size_t>(d_param));
                std::cout << "  Suggested HeavyKeeper configuration within budget:\n"
                          << "    Depth (d): " << static_cast<std::size_t>(d_param) << "\n"
                          << "    Top-K (k): " << static_cast<std::size_t>(k_param) << "\n"
                          << "    Max width (w): " << w_suggested << " columns\n";
            } else {
                std::cout << "  Memory budget is too small to fit the Stream-Summary structure (requires at least " << summary_est << " bytes).\n";
            }
        } else if (structure == "dd_sketch") {
            double g = (1.0 + error) / (1.0 - error);
            double lng = std::log(g);
            std::size_t max_buckets = budget > 64 ? (static_cast<std::size_t>(budget) - 64) / 8 : 0;
            if (max_buckets > 0) {
                double max_ratio = std::exp(max_buckets * lng);
                std::cout << "  Suggested DDSketch configuration within budget:\n"
                          << "    Relative Error (alpha): " << error << "\n"
                          << "    Max dynamic range (V_max / V_min) supported: " << max_ratio << "\n";
            } else {
                std::cout << "  Memory budget is too small for DDSketch basic structures.\n";
            }
        } else {
            std::cout << "  Memory budget scaling fits similarly based on above formulas.\n";
        }
    }

    std::cout << "========================================================\n";
    return 0;
}
