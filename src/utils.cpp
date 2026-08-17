#include <string_view>
#include "utils.h"
#include "Transformers.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <filesystem>
#include <numeric>
#include <map>
#include <set>
#include <thread>
#include <mutex>
#include <iterator>
#include "secrets.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace utils {

const std::vector<std::string> FIND_WORDS = {
    "PILGRIM", "WITHIN", "IT IS ", "STVDY", "MASTER", "STVDENT", 
    "FOLLOW", "SHADOWS", "AETHEREAL", "BVFFERS", "CARNAL", 
    "OBSCVRA", "MOBIVS", "ANALOG", "MOVRNFVL", "CABAL"
};

struct SectionDef {
    std::string name;
    std::vector<size_t> page_indices; // 1-based index conforme o livro
};

const std::vector<SectionDef> G_LIBER_SECTIONS = {
    {"Section_01_Atbash", {1}},
    {"Section_03_04_Vigenere", {3, 4}},
    {"Section_05_GP", {5}},
    {"Section_06_09_AtbashShift", {6, 7, 8, 9}},
    {"Section_10_13_GP", {10, 11, 12, 13}},
    {"Section_14_15_Vigenere", {14, 15}},
    {"Section_16_GP", {16}},
    {"Section_17_19_Unresolved", {17, 18, 19}},
    {"Section_20_24_Unresolved", {20, 21, 22, 23, 24}},
    {"Section_25_31_Unresolved", {25, 26, 27, 28, 29, 30, 31}},
    {"Section_32_39_Unresolved", {32, 33, 34, 35, 36, 37, 38, 39}},
    {"Section_40_43_Unresolved", {40, 41, 42, 43}},
    {"Section_44_49_Unresolved", {44, 45, 46, 47, 48, 49}},
    {"Section_50_56_Unresolved", {50, 51, 52, 53, 54, 55, 56}},
    {"Section_57_70_Unresolved", {57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70}},
    {"Section_71_72_Unresolved", {71, 72}},
    {"Section_73_Totient", {73}},
    {"Section_74_Parable", {74}}
};

// Structure to unify per-page results and ensure a single CSV row per page
struct CipherAnalysisResult {
    std::string page_name;
    size_t rune_count;
    std::string best_cipher;
    int best_param;
    double fitness;
    double ioc;
    int best_vigenere_kl;
    std::string txt_log; // Buffer for heuristic_results.txt
};

const std::wstring GREEN_COLOR = L"\x1b[32m";
const std::wstring RESET_COLOR = L"\x1b[0m";

void setup_console() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
    SetConsoleOutputCP(CP_UTF8);
    _setmode(_fileno(stdout), _O_U8TEXT);
#endif
}

std::array<double, 29> calculate_liber_unigram_target() {
    std::array<size_t, 29> global_counts{};
    size_t total_runes = 0;

    for (const auto& page : core::G_PAGES) {
        if (core::has_known_solution(page.index)) {
            core::ProcessedText pt(page.content, page.index);
            if (core::apply_known_solution(page, pt)) {
                for (auto idx : pt.indices()) {
                    if (idx < 29) {
                        global_counts[idx]++;
                        total_runes++;
                    }
                }
            }
        }
    }

    std::array<double, 29> target_dist;
    if (total_runes == 0) {
        std::array<double, 29> uniform_dist;
        uniform_dist.fill(1.0 / 29.0);
        return uniform_dist;
    }

    for (size_t i = 0; i < 29; ++i) {
        target_dist[i] = static_cast<double>(global_counts[i]) / static_cast<double>(total_runes);
    }

    std::ofstream csv_file("../output/liber_unigram_target.csv");
    if (csv_file.is_open()) {
        csv_file << "RuneIndex,Frequency\n";
        for (size_t i = 0; i < 29; ++i) {
            csv_file << i << "," << std::fixed << std::setprecision(6) << target_dist[i] << "\n";
        }
        csv_file.close();
        std::wcout << L"Liber Unigram Target Distribution saved to ../output/liber_unigram_target.csv\n";
    } else {
        std::wcerr << L"Error: Could not open ../output/liber_unigram_target.csv for writing.\n";
    }
    return target_dist;
}

std::vector<double> calculate_liber_bigram_target() {
    std::vector<size_t> global_counts(841, 0);
    size_t total_bigrams = 0;

    for (const auto& page : core::G_PAGES) {
        if (core::has_known_solution(page.index)) {
            core::ProcessedText pt(page.content, page.index);
            if (core::apply_known_solution(page, pt)) {
                auto dist = pt.bigram_distribution(); // Note: bigram_distribution returns normalized doubles; recompute counts locally for precision
                // Recomputing locally for exact bigram counts
                std::vector<uint8_t> clean;
                for(auto idx : pt.indices()) if(idx < 29) clean.push_back(idx);
                if(clean.size() < 2) continue;
                for(size_t i=0; i < clean.size()-1; ++i) {
                    global_counts[clean[i]*29 + clean[i+1]]++;
                    total_bigrams++;
                }
            }
        }
    }
    std::vector<double> target(841, 1.0/841.0);
    if (total_bigrams > 0)
        for(size_t i=0; i<841; ++i) target[i] = static_cast<double>(global_counts[i]) / static_cast<double>(total_bigrams);
    return target;
}

double score_text_fitness(const core::ProcessedText& pt, const std::array<double, 29>& target_dist) {
    auto dist = pt.runic_distribution();
    // Jensen-Shannon divergence: symmetric and always defined even when q[i]==0
    // JSD(P||Q) = 0.5*KL(P||M) + 0.5*KL(Q||M), where M = 0.5*(P+Q)
    double jsd = 0.0;
    for (size_t i = 0; i < 29; ++i) {
        double p = dist[i];
        double q = target_dist[i];
        double m = 0.5 * (p + q);
        if (m > 0.0) {
            if (p > 0.0) jsd += 0.5 * p * std::log(p / m);
            if (q > 0.0) jsd += 0.5 * q * std::log(q / m);
        }
    }
    return 1.0 / (1.0 + jsd);
}

double score_text_fitness_advanced(const core::ProcessedText& pt, const std::array<double, 29>& target_uni, const std::vector<double>& target_bi) {
    if (pt.rune_count() < 5) return 0.0;
    
    // 1. Unigram Fitness (JSD)
    double uni_fit = score_text_fitness(pt, target_uni);
    
    // 2. Bigram Fitness (Correlation)
    double bi_fit = calculate_correlation(pt.bigram_distribution(), target_bi);
    bi_fit = (bi_fit + 1.0) / 2.0; // Normalizes from [-1,1] to [0,1]

    // 3. Entropy Penalty (Target: ~4.1 - 4.3 bits)
    double ent = pt.entropy();
    double ent_penalty = std::exp(-std::pow(ent - 4.25, 2) / 0.5); // Gaussian centered on the ideal

    return 0.50 * uni_fit + 0.40 * bi_fit + 0.10 * ent_penalty;
}

double calculate_chi_square(const std::array<double, 29>& observed, const std::array<double, 29>& expected, size_t n) {
    double chi = 0.0;
    double dn = static_cast<double>(n);
    for (size_t i = 0; i < 29; ++i) {
        double e = expected[i] * dn;
        if (e > 0.0001) {
            double o = observed[i] * dn;
            chi += ((o - e) * (o - e)) / e;
        }
    }
    return chi;
}

bool is_anagram(std::string s1, std::string s2) {
    s1.erase(std::remove(s1.begin(), s1.end(), ' '), s1.end());
    s2.erase(std::remove(s2.begin(), s2.end(), ' '), s2.end());
    if (s1.length() != s2.length()) return false;
    std::sort(s1.begin(), s1.end());
    std::sort(s2.begin(), s2.end());
    return s1 == s2;
}

std::string_view utf8_take(std::string_view str, size_t n) {
    size_t bytes = 0, count = 0;
    while (count < n && bytes < str.size()) {
        unsigned char c = static_cast<unsigned char>(str[bytes]);
        if      (c < 0x80)            bytes += 1;
        else if ((c & 0xE0) == 0xC0) bytes += 2;
        else if ((c & 0xF0) == 0xE0) bytes += 3;
        else if ((c & 0xF8) == 0xF0) bytes += 4;
        else                          bytes += 1;
        count++;
    }
    return str.substr(0, bytes);
}

long long gcd_vector(const std::vector<long long>& v) {
    if (v.empty()) return 0;
    long long r = v[0];
    for (size_t i = 1; i < v.size(); ++i) r = std::gcd(r, v[i]); // <-- Changed from core::gcd to std::gcd
    return r;
}

double calculate_mutual_ioc(const std::vector<uint8_t>& colA, const std::vector<uint8_t>& colB) {
    if (colA.empty() || colB.empty()) return 0.0;
    
    std::array<double, 29> freqA{0.0}, freqB{0.0};
    for (auto idx : colA) if (idx < 29) freqA[idx]++;
    for (auto idx : colB) if (idx < 29) freqB[idx]++;

    double match_sum = 0.0;
    for (size_t i = 0; i < 29; ++i) {
        match_sum += (freqA[i] * freqB[i]);
    }
    
    return match_sum / (static_cast<double>(colA.size()) * static_cast<double>(colB.size()));
}

void run_cluster_mutual_ioc_analysis(
    const std::vector<size_t>& page_indices_in_cluster, 
    int suspected_key_len, 
    const std::string& output_csv) 
{
    std::ofstream csv(output_csv);
    csv << "PageA,PageB,AvgMutualIoC,SameKeyConfirmed\n";

    for (size_t i = 0; i < page_indices_in_cluster.size(); ++i) {
        for (size_t j = i + 1; j < page_indices_in_cluster.size(); ++j) {
            
            const auto& pageA = core::G_PAGES[page_indices_in_cluster[i]];
            const auto& pageB = core::G_PAGES[page_indices_in_cluster[j]];
            
            core::ProcessedText ptA(pageA.content);
            core::ProcessedText ptB(pageB.content);

            // Splits both pages into columns based on keystream length
            std::vector<std::vector<uint8_t>> colsA(suspected_key_len);
            std::vector<std::vector<uint8_t>> colsB(suspected_key_len);

            size_t posA = 0;
            for (auto idx : ptA.indices()) {
                if (idx < 29) colsA[posA++ % suspected_key_len].push_back(idx);
            }
            size_t posB = 0;
            for (auto idx : ptB.indices()) {
                if (idx < 29) colsB[posB++ % suspected_key_len].push_back(idx);
            }

            double total_mutual_ioc = 0.0;
            for (int k = 0; k < suspected_key_len; ++k) {
                total_mutual_ioc += calculate_mutual_ioc(colsA[k], colsB[k]);
            }
            double avg_mutual_ioc = total_mutual_ioc / suspected_key_len;
            
            // If the average MIC crosses 0.052, the structural correlation is strong and identical
            bool confirmed = (avg_mutual_ioc > 0.052);

            csv << pageA.name << "," << pageB.name << "," 
                << std::fixed << std::setprecision(5) << avg_mutual_ioc << "," 
                << (confirmed ? "YES" : "NO") << "\n";
        }
    }
    csv.close();
}

double calculate_ioc_from_indices(const std::vector<uint8_t>& indices) {
    if (indices.size() <= 1) return 0.0;
    std::array<size_t, 29> freqs{};
    size_t count = 0;
    for (auto idx : indices) if (idx < 29) { freqs[idx]++; count++; }
    if (count <= 1) return 0.0;
    double sum = 0.0;
    for (size_t f : freqs) { double df = static_cast<double>(f); sum += df * (df - 1.0); }
    double N = static_cast<double>(count);
    return sum / (N * (N - 1.0));
}

void save_correlation_csv(const std::string& filename, const std::vector<PageInfo>& pages, bool is_runic) {
    std::ofstream csv(filename);
    if (!csv.is_open()) return;
    csv << "Page";
    for (const auto& p : pages) csv << "," << p.name;
    csv << "\n";
    for (const auto& p1 : pages) {
        csv << p1.name;
        for (const auto& p2 : pages) {
            double corr = is_runic 
                ? calculate_correlation(p1.runic_unigram, p2.runic_unigram)
                : calculate_correlation(p1.latin_unigram, p2.latin_unigram);
            csv << "," << std::fixed << std::setprecision(6) << corr;
        }
        csv << "\n";
    }
    std::wcout << L"Matrix saved to: " << std::wstring(filename.begin(), filename.end()) << "\n";
}

void run_statistical_analysis(int num_threads) {
    std::ofstream f("../output/analysis.txt");
    if (!f.is_open()) return;
    f << "=== LIBER PRIMUS STATISTICAL ANALYSIS ===\n\n";
    
    // Auxiliary structures to capture real numeric index of the page
    // This avoids alphabetic sorting errors (e.g. Page 10 before Page 2)
    struct OrderedPageInfo {
        size_t original_index;
        PageInfo info;
    };
    
    struct OrderedTextOutput {
        size_t original_index;
        std::string text;
    };

    std::vector<OrderedPageInfo> temp_pages;
    std::vector<OrderedTextOutput> temp_outputs; 
    std::mutex mtx;

    std::array<double, 29> liber_unigram_target = calculate_liber_unigram_target();
    const auto& pages = core::G_PAGES;

    auto worker = [&](size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            const auto& page = pages[i];
            if (page.content.empty()) continue;

            core::ProcessedText pt(page.content);
            if (pt.rune_count() == 0) continue;

            double unif_ioc = 1.0 / 29.0;
            double fitness = score_text_fitness(pt, liber_unigram_target);
            double chi = calculate_chi_square(pt.runic_distribution(), liber_unigram_target, pt.rune_count());
            double ioc_dev = (pt.index_of_coincidence() - unif_ioc) / unif_ioc * 100.0;

            // 1. Buffer report in thread-local memory
            std::ostringstream oss;
            oss << "Page: " << page.name << " (Runes: " << pt.rune_count() << ")\n"
                << "  - Runic IoC:   " << pt.index_of_coincidence() << " (" << (ioc_dev >= 0 ? "+" : "") << std::fixed << std::setprecision(1) << ioc_dev << "% vs random)\n"
                << "  - Entropy:     " << pt.entropy() << " bits\n"
                << "  - Chi-Square:  " << chi << "\n"
                << "  - Fitness:     " << std::fixed << std::setprecision(4) << fitness << "\n\n";

            // 2. Minimal protection with mutex for shared vector writing only
            {
                std::lock_guard<std::mutex> lock(mtx);
                std::wstring wname(page.name.begin(), page.name.end());
                std::wcout << L"[THREAD " << std::this_thread::get_id() << L"] Analyzing " << wname << L"..." << std::endl;

                temp_pages.push_back({ page.index, {std::string(page.name), pt.runic_distribution(), pt.bigram_distribution(), pt.latin_distribution()} });
                temp_outputs.push_back({ page.index, oss.str() });
            }
        }
    };

    // Parallel thread creation and execution
    std::vector<std::thread> threads;
    size_t pages_per_thread = pages.size() / num_threads;
    for (int t = 0; t < num_threads; ++t) {
        size_t start = t * pages_per_thread;
        size_t end = (t == num_threads - 1) ? pages.size() : (t + 1) * pages_per_thread;
        threads.emplace_back(worker, start, end);
    }
    for (auto& t : threads) t.join();

    // 3. Strictly NUMERICAL sorting based on real Liber Primus index
    std::sort(temp_pages.begin(), temp_pages.end(), [](const OrderedPageInfo& a, const OrderedPageInfo& b) {
        return a.original_index < b.original_index;
    });

    std::sort(temp_outputs.begin(), temp_outputs.end(), [](const OrderedTextOutput& a, const OrderedTextOutput& b) {
        return a.original_index < b.original_index;
    });

    // 4. Reconstruct flat PageInfo vector expected by CSV save functions
    std::vector<PageInfo> all_pages;
    all_pages.reserve(temp_pages.size());
    for (const auto& item : temp_pages) {
        all_pages.push_back(item.info);
    }

    // 5. Write to text file in sequential order
    for (const auto& out : temp_outputs) {
        f << out.text;
    }

    // 6. Saving CSVs that feed the Python Dashboard
    save_correlation_csv("../output/corr_runic.csv", all_pages, true);
    save_correlation_csv("../output/corr_latin.csv", all_pages, false);
    
    f.close();
}

void run_friedman_key_length_scan(const std::string& output_csv_path) {
    std::ofstream csv(output_csv_path);
    std::string txt_path = output_csv_path;
    if (txt_path.find(".csv") != std::string::npos) 
        txt_path.replace(txt_path.find(".csv"), 4, ".txt");
    else 
        txt_path += ".txt";
    
    std::ofstream txt(txt_path);
    if (!csv.is_open() || !txt.is_open()) return;
    
    csv << "Page,KeyLen,AvgColIoC\n";
    txt << "=== COMPREHENSIVE KEY LENGTH ANALYSIS (FRIEDMAN + KASISKI) ===\n\n";

    const auto& pages = core::G_PAGES;

    for (const auto& page : pages) {
        if (page.content.empty() || core::has_known_solution(page.index)) continue;

        core::ProcessedText pt(page.content, page.index);
        std::vector<uint8_t> runes;
        for (auto idx : pt.indices()) if (idx < 29) runes.push_back(idx);

        if (runes.size() < 40) continue;

        txt << "Page: " << page.name << " (" << runes.size() << " runes)\n";

        // 1. Componente Kasiski: Encontra fatores de distâncias entre padrões repetidos
        std::map<int, int> kasiski_votes;
        std::map<std::string, std::vector<size_t>> patterns;
        for (size_t i = 0; i <= runes.size() - 3; ++i) {
            std::string pat;
            for(int k=0; k<3; ++k) pat += static_cast<char>(runes[i+k]);
            patterns[pat].push_back(i);
        }
        for (auto const& [pat, positions] : patterns) {
            if (positions.size() > 1) {
                for (size_t j = 1; j < positions.size(); ++j) {
                    int d = static_cast<int>(positions[j] - positions[j-1]);
                    // Vote for all distance factors found (up to 30)
                    for (int f = 2; f <= 30; ++f) if (d % f == 0) kasiski_votes[f]++;
                }
            }
        }

        // 2. Componente Friedman: Varredura de IoC Colunar
        struct FResult { int kl; double ioc; };
        std::vector<FResult> friedman_results;

        for (int kl = 2; kl <= 30; ++kl) {
            std::vector<std::array<double, 29>> col_dists(kl, std::array<double, 29>{0.0});
            std::vector<size_t> col_counts(kl, 0);
            
            for (size_t i = 0; i < runes.size(); ++i) {
                col_dists[i % kl][runes[i]]++;
                col_counts[i % kl]++;
            }

            double avg_col_ioc = 0.0;
            for (int c = 0; c < kl; ++c) {
                if (col_counts[c] <= 1) continue;
                double sum = 0.0;
                double N = static_cast<double>(col_counts[c]);
                for (size_t i = 0; i < 29; ++i) {
                    sum += col_dists[c][i] * (col_dists[c][i] - 1);
                }
                avg_col_ioc += sum / (N * (N - 1.0));
            }
            avg_col_ioc /= kl;

            friedman_results.push_back({kl, avg_col_ioc});
            csv << page.name << "," << kl << "," << std::fixed << std::setprecision(5) << avg_col_ioc << "\n";
        }

        // Sort Friedman results to find peaks
        std::sort(friedman_results.begin(), friedman_results.end(), [](auto& a, auto& b){ return a.ioc > b.ioc; });
        
        txt << "  Friedman Peaks: ";
        for (size_t i = 0; i < std::min((size_t)5, friedman_results.size()); ++i)
            txt << friedman_results[i].kl << "(" << std::fixed << std::setprecision(4) << friedman_results[i].ioc << ") ";
        
        if (!kasiski_votes.empty()) {
            std::vector<std::pair<int, int>> sorted_k(kasiski_votes.begin(), kasiski_votes.end());
            std::sort(sorted_k.begin(), sorted_k.end(), [](auto& a, auto& b){ return a.second > b.second; });
            txt << "\n  Kasiski Votes:  ";
            for (size_t i = 0; i < std::min((size_t)5, sorted_k.size()); ++i)
                txt << sorted_k[i].first << "(" << sorted_k[i].second << "v) ";
        }

        // Highlight if both methods agree
        if (friedman_results[0].ioc > 0.05 && kasiski_votes[friedman_results[0].kl] > 0) {
            txt << "\n  [!!!] HIGH CONFIDENCE candidate: Key Length " << friedman_results[0].kl;
        }
        txt << "\n------------------------------------------\n\n";
    }
    txt.close();
    csv.close();
    std::wcout << L"Scan complete. Results saved to " << std::wstring(output_csv_path.begin(), output_csv_path.end()) << L" (.csv and .txt)\n";
}

void run_heuristic_cipher_analysis(int num_threads) {
    std::wcout << L"\n=== HEURISTIC CIPHER ESTIMATOR ===\n";
    std::ofstream f("../output/heuristic_results.txt");
    std::ofstream csv("../output/heuristic_scores.csv");
    
    if (!f.is_open() || !csv.is_open()) return;

    csv << "Page,Length,BestCipher,BestParam,Fitness,IoC,BestVigenereKeyLen\n";
    auto target_dist = calculate_liber_unigram_target();
    
    std::vector<CipherAnalysisResult> final_results;
    std::mutex mtx;
    const auto& pages = core::G_PAGES;

    auto worker = [&](size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            const auto& page = pages[i];
            if (core::has_known_solution(page.index)) continue;
        
            core::ProcessedText pt_original(page.content, page.index);
            if (pt_original.rune_count() == 0) continue;

            struct Result { std::string type; int param; double fitness; };
            std::vector<Result> mono_results;

            // 0. Test Identity
            mono_results.push_back({"None", 0, score_text_fitness(pt_original, target_dist)});

            // 1. Test Caesar Shifts
            for (int s = 0; s < 29; ++s) {
                core::ProcessedText pt = pt_original;
                core::ShiftTransformer(s).transform(pt);
                mono_results.push_back({"Shift", s, score_text_fitness(pt, target_dist)});
            }

            // 2. Test Atbash Variations
            for (int s = 0; s < 29; ++s) {
                core::ProcessedText pt = pt_original;
                core::AtbashTransformer(s).transform(pt);
                mono_results.push_back({"Atbash", s, score_text_fitness(pt, target_dist)});
            }

            std::sort(mono_results.begin(), mono_results.end(), [](const Result& a, const Result& b) {
                return a.fitness > b.fitness;
            });

            // 3. Vigenere brute-force by estimated length (PARALLEL)
            double ioc = pt_original.index_of_coincidence();
            auto auto_corr = pt_original.autocorrelation();
            int auto_corr_lag = 0;
            double max_val = 0;
            for (size_t lag = 2; lag < auto_corr.size(); ++lag) {
                if (auto_corr[lag] > max_val) {
                    max_val = auto_corr[lag];
                    auto_corr_lag = static_cast<int>(lag);
                }
            }

            std::vector<int> key_lengths_to_try;
            if (max_val > 0.06) {
                key_lengths_to_try = {auto_corr_lag, auto_corr_lag * 2, auto_corr_lag / 2};
            }
            for (int kl : {7, 8, 13}) {
                if (std::find(key_lengths_to_try.begin(), key_lengths_to_try.end(), kl) == key_lengths_to_try.end())
                    key_lengths_to_try.push_back(kl);
            }

            int best_vigenere_kl = 0;
            double max_avg_col_ioc = 0.0;
            std::ostringstream vigenere_oss;

            for (int kl : key_lengths_to_try) {
                if (kl < 2 || kl > 20) continue;
                std::vector<std::array<double, 29>> col_dists(kl);
                std::vector<size_t> col_counts(kl, 0);
                size_t rune_pos = 0;
                for (auto idx : pt_original.indices()) {
                    if (idx >= 29) continue;
                    col_dists[rune_pos % kl][idx]++;
                    col_counts[rune_pos % kl]++;
                    rune_pos++;
                }
                double avg_col_ioc = 0.0;
                    for (int c = 0; c < kl; ++c) {
                        if (col_counts[c] <= 1) continue;
                        double sum = 0.0;
                        double N = static_cast<double>(col_counts[c]);
                        for (size_t i = 0; i < 29; ++i) sum += col_dists[c][i] * (col_dists[c][i] - 1);
                        avg_col_ioc += sum / (N * (N - 1.0));
                    }
                avg_col_ioc /= kl;

                if (avg_col_ioc > 0.055) {
                    vigenere_oss << "    [!] KeyLen=" << kl << " ColIoC=" << std::fixed << std::setprecision(4) << avg_col_ioc << " (Friedman hit — likely key)\n";
                    if (avg_col_ioc > max_avg_col_ioc) {
                        max_avg_col_ioc = avg_col_ioc;
                        best_vigenere_kl = kl;
                    }
                }
            }

            // Build text log in memory
            std::ostringstream txt_oss;
            txt_oss << "Page: " << page.name << " (Length: " << pt_original.rune_count() << ")\n"
                    << "  Top Estimations:\n";
            size_t num_display = std::min((size_t)5, mono_results.size());
            for (size_t i = 0; i < num_display; ++i) {
                txt_oss << "    " << i+1 << ". " << std::setw(8) << std::left << mono_results[i].type << " (param: " << std::setw(2) << mono_results[i].param 
                        << ") Fitness: " << std::fixed << std::setprecision(5) << mono_results[i].fitness << "\n";
            }
            txt_oss << vigenere_oss.str() // Insert Vigenère hits if any
                    << "  Baseline IoC: " << ioc << (ioc > 0.06 ? " (High: Likely Substitution)" : " (Low: Likely Polyalphabetic)") << "\n"
                    << "------------------------------------------\n";

            // Determine final data for single CSV row
            std::string csv_cipher = mono_results[0].type;
            int csv_param = mono_results[0].param;
            double csv_fitness = mono_results[0].fitness;
            
            // Treatment for best Vigenère key length column
            int final_vigenere_col = 0;
            if (best_vigenere_kl > 0) {
                final_vigenere_col = best_vigenere_kl;
            } else if (max_val > 0.08) {
                final_vigenere_col = auto_corr_lag;
            }

            // If Vigenère column test is strong, it wins as the likely cipher
            if (max_avg_col_ioc > 0.055) {
                csv_cipher = "Vigenere";
                csv_param = best_vigenere_kl;
                csv_fitness = max_avg_col_ioc; // Consolidate column IoC as fitness
            }

            // Fast lock for result saving
            {
                std::lock_guard<std::mutex> lock(mtx);
                final_results.push_back({
                    std::string(page.name),
                    pt_original.rune_count(),
                    csv_cipher,
                    csv_param,
                    csv_fitness,
                    ioc,
                    final_vigenere_col,
                    txt_oss.str()
                });
                std::wcout << L"Analyzed " << std::wstring(page.name.begin(), page.name.end()) << L"\n";
            }
        }
    };

    // Dispatch threads
    std::vector<std::thread> threads;
    size_t pages_per_thread = pages.size() / num_threads;
    for (int t = 0; t < num_threads; ++t) {
        size_t start = t * pages_per_thread;
        size_t end = (t == num_threads - 1) ? pages.size() : (t + 1) * pages_per_thread;
        threads.emplace_back(worker, start, end);
    }
    for (auto& t : threads) t.join();

    // Deterministic sorting
    std::sort(final_results.begin(), final_results.end(), [](const CipherAnalysisResult& a, const CipherAnalysisResult& b) {
        return a.page_name < b.page_name;
    });

    // Write outputs cleanly
    for (const auto& res : final_results) {
        f << res.txt_log;

        csv << res.page_name << ","
            << res.rune_count << ","
            << res.best_cipher << ","
            << res.best_param << ","
            << std::fixed << std::setprecision(5) << res.fitness << ","
            << std::fixed << std::setprecision(5) << res.ioc << ","
            << res.best_vigenere_kl << "\n";
    }

    csv.close();
    f.close();
    std::wcout << L"Results saved to ../output/heuristic_results.txt\n";
    std::wcout << L"CSV scores saved to ../output/heuristic_scores.csv\n";
}

void run_oeis_search(const std::vector<core::OEISSequence>& sequences, std::ofstream& outputFile, int num_threads)
{
    size_t resumePage = 7;
    std::string resumeSeqId;
    std::mutex mtx;

    std::ifstream ckpt("../output/stopped-at.txt");
    if (ckpt.is_open()) {
        std::string pl, sl;
        if (std::getline(ckpt, pl) && std::getline(ckpt, sl)) {
            try {
                if (pl.find("PageIndex: ") == 0) resumePage = std::stoul(pl.substr(11));
                if (sl.find("Sequence: ")  == 0) resumeSeqId = sl.substr(10);
            } catch (...) {}
        }
        std::wcout << L"Resuming from Page Index " << resumePage
                   << L", Sequence "
                   << std::wstring(resumeSeqId.begin(), resumeSeqId.end()) << L"\n";
    }

    // Iterate through pages starting from checkpoint
    for (size_t p = resumePage; p < core::G_PAGES.size(); ++p)
    {
        const auto& page = core::G_PAGES[p];
        if (core::has_known_solution(page.index)) continue;

        std::string_view page_name = page.name;
        std::wcout << L"\nScanning "
                   << std::wstring(page_name.begin(), page_name.end()) << L"...\n";

        // Analyze whole page
        core::ProcessedText base_pt(page.content, page.index);
        
        // Collect ᚠ rune positions for power-set interruptions
        std::vector<size_t> f_positions;
        const auto& base_idx = base_pt.indices();
        size_t rune_counter = 0;
        for (size_t i = 0; i < base_idx.size(); ++i) {
            if (base_idx[i] < 29) {
                if (base_idx[i] == 0) f_positions.push_back(rune_counter);
                rune_counter++;
            }
        }

        if (f_positions.size() > 30) {
            std::wcout << L"Skipping (too many F-runes)\n";
            continue;
        }

        size_t start_seq_idx = 0;
        if (!resumeSeqId.empty()) {
            for (size_t i = 0; i < sequences.size(); ++i) {
                if (sequences[i].id == resumeSeqId) { start_seq_idx = i; break; }
            }
        }
        
        auto worker = [&](size_t start_s, size_t end_s) {
            auto target_dist = calculate_liber_unigram_target();
            for (size_t s = start_s; s < end_s; ++s) {
                const auto& seq = sequences[s];
                
            unsigned long long combos = 1ULL << f_positions.size();
            for (size_t mask = 0; mask < combos; ++mask) {
                std::vector<size_t> interrupts;
                for (size_t j = 0; j < f_positions.size(); ++j)
                    if ((mask >> j) & 1) interrupts.push_back(f_positions[j]);

                core::ProcessedText pt = base_pt;
                core::SequenceTransformer t(seq.data, interrupts);
                t.transform(pt);

                // Fast statistical filter before string searching
                if (score_text_fitness(pt, target_dist) < 0.75) continue;

                std::string latin = pt.to_latin();
                for (const auto& word : FIND_WORDS) {
                    if (latin.find(word) == std::string::npos) continue;

                    {
                        std::lock_guard<std::mutex> lock(mtx);
                        print_hit("OEIS", core::G_PAGES[p], 0, word, latin, seq.id);

                        outputFile << "page: " << core::G_PAGES[p].name << "\n"
                                   << seq.id << ": ";
                        for (size_t k = 0; k < seq.data.size(); ++k)
                            outputFile << seq.data[k] << (k + 1 < seq.data.size() ? ", " : "");
                        outputFile << "\nfind_word: " << word
                                   << "\ndecrypted: " << latin
                                   << "\n----------------------------------------\n";
                    }
                }
            }
        }
        };

        std::vector<std::thread> threads;
        size_t total_to_process = sequences.size() - start_seq_idx;
        size_t chunk = total_to_process / num_threads;
        for (int t = 0; t < num_threads; ++t) {
            size_t start = start_seq_idx + t * chunk;
            size_t end = (t == num_threads - 1) ? sequences.size() : start_seq_idx + (t + 1) * chunk;
            if (start < end) threads.emplace_back(worker, start, end);
        }
        for (auto& t : threads) t.join();

        resumeSeqId.clear();
        std::wcout << L"Page " << std::wstring(page_name.begin(), page_name.end())
                   << L" complete.\n";
    }
    std::wcout << L"OEIS Attack finished.\n";
}


void print_hit(const std::string& type, const core::Page& page, int offset,
               const std::string& word, const std::string& latin,
               const std::string& mode_str)
{
    std::wstring wword(word.begin(), word.end());
    std::wstring wpage(page.name.begin(), page.name.end());
    std::wstring wmode(mode_str.begin(), mode_str.end());

    std::wcout << GREEN_COLOR << L"[HIT " << std::wstring(type.begin(), type.end()) << L"] "
               << RESET_COLOR << L"Page: " << wpage
               << L" | Mode: " << wmode << L" | Offset: " << offset
               << L" | Word: " << GREEN_COLOR << wword << RESET_COLOR << L"\n"
               << L"Preview: "
               << std::wstring(latin.begin(), latin.end()).substr(0, 80) << L"...\n"
               << L"---------------------------------------------------\n";
}

void run_rolling_ioc_analysis(size_t window_size, int num_threads) {
    std::wcout << L"\n=== ROLLING IoC METROLOGY (Window: " << window_size << L") ===\n";
    std::ofstream f("../output/rolling_ioc_analysis.txt");
    std::ofstream csv("../output/rolling_ioc_peaks.csv");
    csv << "Page,Position,IoC\n";

    std::mutex mtx;

    const auto& pages = core::G_PAGES;
    auto worker = [&](size_t start, size_t end) {
        for (size_t idx_p = start; idx_p < end; ++idx_p) {
            const auto& page = pages[idx_p];
        if (page.content.empty()) continue;
        core::ProcessedText pt(page.content, page.index);
        auto& indices = pt.indices();
        
        // Filter only runes for IoC calculation
        std::vector<uint8_t> clean_runes;
        for(auto idx : indices) if(idx < 29) clean_runes.push_back(idx);

        std::stringstream ss;
        bool resolved = core::has_known_solution(page.index);
        ss << "Page: " << page.name << (resolved ? " [STATUS: RESOLVED]" : " [STATUS: UNRESOLVED]") << "\n";

        double global_ioc = pt.index_of_coincidence();
        ss << "  Global Page IoC: " << std::fixed << std::setprecision(5) << global_ioc << "\n";
        
        // Reference: 0.034 = Random | 0.067 = Plaintext
        if (global_ioc < 0.030) ss << "  [NOTE] High Diffusion detected (IoC below random). Likely complex polyalphabetic.\n";

        if (clean_runes.size() < window_size) {
            ss << "  (Page too short for rolling analysis)\n";
            ss << "------------------------------------------\n";
            std::lock_guard<std::mutex> lock(mtx);
            f << ss.str();
            continue;
        }

        struct Peak { size_t pos; double ioc; };
        std::vector<Peak> peaks;

        for (size_t i = 0; i <= clean_runes.size() - window_size; ++i) {
            std::vector<uint8_t> window(clean_runes.begin() + i, clean_runes.begin() + i + window_size);
            double ioc = calculate_ioc_from_indices(window);
            peaks.push_back({i, ioc});
        }

        // Copy to sort results for text log (.txt) only
        // This allows seeing high interest points first without cluttering the CSV
        std::vector<Peak> sorted_peaks = peaks;
        std::sort(sorted_peaks.begin(), sorted_peaks.end(), [](const Peak& a, const Peak& b) {
            return std::abs(a.ioc - 0.0667) < std::abs(b.ioc - 0.0667);
        });

        for (size_t i = 0; i < std::min((size_t)15, sorted_peaks.size()); ++i) {
            double ioc = sorted_peaks[i].ioc;
            
            ss << "  Pos " << std::setw(4) << sorted_peaks[i].pos << ": IoC " << std::fixed << std::setprecision(4) << ioc;
            if (ioc >= 0.060 && ioc <= 0.075) {
                ss << " [!!! TARGET PROXIMITY]";
            }
            else if (ioc < 0.025)  ss << " [DIFUSION/FLATTENING]";
            else if (ioc > 0.075)  ss << " [HIGH REPETITION]";
            
            ss << "\n";
        }

        ss << "------------------------------------------\n";
        
        {
            std::lock_guard<std::mutex> lock(mtx);
            f << ss.str();
            // Save ALL IoC data to CSV
            for(const auto& peak : peaks) {
                csv << page.name << "," << peak.pos << "," << peak.ioc << "\n";
            }
            std::wcout << L"Processed " << std::wstring(page.name.begin(), page.name.end()) << L"\n";
        }
        }
    };

    std::vector<std::thread> threads;
    size_t pages_per_thread = pages.size() / num_threads;
    for (int t = 0; t < num_threads; ++t) {
        size_t start = t * pages_per_thread;
        size_t end = (t == num_threads - 1) ? pages.size() : (t + 1) * pages_per_thread;
        threads.emplace_back(worker, start, end);
    }
    for (auto& t : threads) t.join();

    std::wcout << L"Analysis saved to ../output/rolling_ioc_analysis.txt\n";
    std::wcout << L"Peaks exported to ../output/rolling_ioc_peaks.csv\n";
}

void run_peak_bruteforce_analysis() {
    std::wcout << L"\n=== PEAK BRUTE-FORCE ANALYSIS (Local Windows) ===\n";
    std::ifstream csv("../output/rolling_ioc_peaks.csv");
    if (!csv.is_open()) {
    std::wcerr << L"Error: run option 2 first to generate peaks.\n";
        return;
    }

    auto target_dist = calculate_liber_unigram_target();
    std::string line;
    std::getline(csv, line); // Skip header

    while (std::getline(csv, line)) {
        std::stringstream ss(line);
        std::string page_name, pos_str, ioc_str;
        std::getline(ss, page_name, ',');
        std::getline(ss, pos_str, ',');
        std::getline(ss, ioc_str, ',');

        size_t pos = std::stoul(pos_str);

        // Find page in global data
        for (const auto& pg : core::G_PAGES) {
            if (pg.name == page_name) {
                core::ProcessedText pt_full(pg.content);
                auto& full_indices = pt_full.indices();
                
                // Extract only window runes
                std::vector<uint8_t> window_indices;
                std::vector<size_t> original_map;
                size_t r_count = 0;
                for(size_t i=0; i<full_indices.size(); ++i) {
                    if(full_indices[i] < 29) {
                        if(r_count >= pos && r_count < pos + 30) {
                            window_indices.push_back(full_indices[i]);
                            original_map.push_back(i);
                        }
                        r_count++;
                    }
                }

                // If global Page IoC is too low, warn that simple brute-force may fail
                bool resolved = core::has_known_solution(pg.index);
                if (!resolved && pt_full.index_of_coincidence() < 0.04) {
                    static std::set<std::string> warned_pages;
                    if (warned_pages.find(pg.name.data()) == warned_pages.end()) {
                        std::wcout << L"[INFO] " << std::wstring(pg.name.begin(), pg.name.end()) << L" has low IoC. Consider rhythmic attacks.\n";
                        warned_pages.insert(std::string(pg.name.data()));
                    }
                }

                if(window_indices.empty()) continue;

                // Local brute force on window
                for(int s=0; s<29; ++s) {
                    for(bool atbash : {false, true}) {
                        std::vector<uint8_t> test = window_indices;
                                for(auto& idx : test) {
                                    int tmp;
                                    if (atbash) tmp = (28 - static_cast<int>(idx) + s + 29) % 29;
                                    else tmp = (static_cast<int>(idx) + s + 29) % 29;
                                    idx = static_cast<uint8_t>(tmp);
                                }

                        core::ProcessedText pt_res("", pg.index);
                        pt_res.indices() = test;
                        double fitness = score_text_fitness(pt_res, target_dist);
                        std::string latin = pt_res.to_latin();

                        if (fitness > 0.89) {
                            bool hit = false;
                            for(const auto& w : FIND_WORDS) if(latin.find(w) != std::string::npos) hit = true;

                            if (hit || fitness > 0.94) {
                                std::wcout << (hit ? GREEN_COLOR : L"")
                                           << (resolved ? L"[KNOWN HIT] " : L"[NEW POTENTIAL] ")
                                           << L"Page: " << std::wstring(page_name.begin(), page_name.end())
                                           << L" | Pos: " << pos << L" | Method: " << (atbash ? L"Atbash+" : L"Shift ") << s 
                                           << L" | Fit: " << fitness << RESET_COLOR << L"\n"
                                           << L"   Preview: " << std::wstring(latin.begin(), latin.end()) << L"\n";
                            }
                        }
                    }
                }
            }
        }
    }
}

void run_key_sequence_analysis() {
    std::ofstream f("../output/key_stream_analysis.txt");
    std::ofstream csv("../output/key_stream_analysis.csv");
    csv << "Page,Position,KeyValue\n";

    for (size_t i = 0; i < core::G_PAGES.size(); ++i) {
        const auto& pg = core::G_PAGES[i];
        if (pg.content.empty()) continue;

        core::ProcessedText pt_cipher(pg.content);
        core::ProcessedText pt_plain(pg.content, pg.index);

        bool solved = core::apply_known_solution(pg, pt_plain);

        const auto& c_idx = pt_cipher.indices();
        const auto& p_idx = pt_plain.indices();
        std::vector<int> stream;

        for (size_t j = 0; j < c_idx.size(); ++j) {
            if (c_idx[j] < 29 && p_idx[j] < 29)
                stream.push_back((static_cast<int>(c_idx[j]) - static_cast<int>(p_idx[j]) + 29) % 29);
        }

        f << "Page: " << pg.name << (solved ? "" : " [UNRESOLVED]") << " | Values: ";
        if (stream.empty()) {
            f << "[NO RUNES]\n";
        } else {
            for (size_t j = 0; j < std::min(stream.size(), (size_t)20); ++j) f << stream[j] << " ";
            f << (stream.size() > 20 ? "...\n" : "\n");
        }

        if (!solved) csv << pg.name << ",-1,UNRESOLVED\n";

        // Export full stream to CSV for dashboard
        for (size_t j = 0; j < stream.size(); ++j) {
            csv << pg.name << "," << j << "," << stream[j] << "\n";
        }
    }
    std::wcout << L"Key stream analysis saved to ../output/key_stream_analysis.txt\n";
    std::wcout << L"Full key stream CSV saved to ../output/key_stream_analysis.csv\n";
}

void run_skip_index_analysis() {
    std::ofstream f("../output/skip_index_analysis.txt");
    for (size_t i = 0; i < core::G_PAGES.size(); ++i) {
        const auto& pg = core::G_PAGES[i];
        std::vector<size_t> interrupts = core::get_possible_interrupters(pg.index);
        if (interrupts.empty()) continue;
        f << "Page: " << pg.name << " | Gaps: ";
        for (size_t j = 1; j < interrupts.size(); ++j) f << (interrupts[j] - interrupts[j-1]) << " ";
        f << "\n";
    }
}

void run_interrupt_geometry_analysis() {
    std::wcout << L"\n=== INTERRUPT GEOMETRY & CLUSTER ANALYSIS ===\n";
    std::ofstream f("../output/interrupt_clusters.txt");
    std::ofstream csv("../output/interrupt_deltas.csv");
    csv << "Page,Delta\n";
    
    struct PageEntry {
        int page_id;
        std::string name;
        std::vector<size_t> indices;
    };
    std::vector<PageEntry> entries;

    // 1. Data collection and sequence display
    f << "--- SEQUENCE OF INTERRUPT COUNTS ---\n";
    for (size_t i = 0; i < core::G_PAGES.size(); ++i) {
        const auto& pg = core::G_PAGES[i];
        std::vector<size_t> interrupts = core::get_possible_interrupters(pg.index);
        entries.push_back(PageEntry{(int)pg.index, std::string(pg.name), interrupts});
        
        f << pg.index << "\tCount: " << interrupts.size() << "\n";

        if (interrupts.size() > 1) {
            for (size_t j = 1; j < interrupts.size(); ++j) {
                long long d = static_cast<long long>(interrupts[j]) - interrupts[j-1];
                csv << pg.index << "," << d << "\n";
            }
        }
    }
    f << "\n";
    csv.close();

    // 2. Cluster Identification (Pages with identical signatures)
    f << "--- STRUCTURAL CLUSTERS (Identical Signatures) ---\n";
    std::map<std::vector<size_t>, std::vector<int>> clusters;
    for (const auto& entry : entries) {
        if (entry.indices.empty()) continue;
        clusters[entry.indices].push_back(entry.page_id);
    }

    int cluster_id = 1;
    for (auto const& [sig, pages] : clusters) {
        if (pages.size() > 1) {
            f << "Cluster #" << cluster_id++ << " (" << sig.size() << " interrupts)\n";
            f << "  Pages: ";
            for(int p : pages) f << p << " ";
            f << "\n  Indices: ";
            for (size_t idx : sig) f << idx << " ";
            
            if (sig.size() > 1) {
                f << "\n  Deltas:  ";
                std::vector<long long> deltas;
                for (size_t j = 1; j < sig.size(); ++j) {
                    long long d = static_cast<long long>(sig[j]) - sig[j-1];
                    deltas.push_back(d);
                    f << d << " ";
                }
                long long g = gcd_vector(deltas);
                f << "\n  GCD of Deltas: " << g;
            }
            f << "\n\n";
        }
    }

    // 3. Anomaly Investigation (Page 16 and neighbors)
    f << "--- ANOMALY INVESTIGATION ---\n";
    for (const auto& entry : entries) {
        if (entry.page_id == 16) {
            f << "Focus Page 16:\n";
            f << "  Indices: ";
            for (size_t idx : entry.indices) f << idx << " ";
            if (entry.indices.size() >= 2) {
                f << "\n  Delta: " << (entry.indices[1] - entry.indices[0]);
            }
            f << "\n  Context: Page 16 often acts as a bridge or a structural null.\n";
        }
    }

    // 4. Raw count sequence export
    f << "\n--- RAW COUNT SEQUENCE (Possible Meta-Message) ---\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        f << entries[i].indices.size() << (i == entries.size() - 1 ? "" : ", ");
    }
    f << "\n";

    f.close();
    
    std::wcout << L"Analysis of " << entries.size() << L" pages concluded.\n";
    std::wcout << GREEN_COLOR << L"Clusters and raw sequences saved to ../output/interrupt_clusters.txt" << RESET_COLOR << L"\n";
    
    // Print sequence to console
    std::wcout << L"\nRaw Interrupt Count Sequence:\n";
    for (const auto& e : entries) std::wcout << e.indices.size() << L" ";
    std::wcout << L"\n";
}

void run_doublet_analysis(int num_threads) {
    std::wcout << L"\n=== DOUBLET (REPETITION) ANALYSIS ===\n";
    std::ofstream f("../output/doublet_analysis.txt");
    std::mutex mtx;

    const auto& pages = core::G_PAGES;
    auto worker = [&](size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            const auto& page = pages[i];
        if (page.content.empty()) continue;
        core::ProcessedText pt(page.content);
        auto& idx = pt.indices();
        
        int doublets = 0;
        std::map<uint8_t, int> doublet_map;
        std::vector<int> runs;
        int current_run = 1;

        for (size_t i = 1; i < idx.size(); ++i) {
            if (idx[i] < 29 && idx[i] == idx[i-1]) { doublets++; doublet_map[idx[i]]++; current_run++; }
            else { if(current_run > 1) runs.push_back(current_run); current_run = 1; }
        }

        double ratio = (pt.rune_count() > 0) ? static_cast<double>(doublets) / static_cast<double>(pt.rune_count()) : 0.0;
        if (ratio > 0.02) { // Peaks > 2% suggest non-random structure
            std::stringstream ss;
            ss << "Page " << page.name << " | Doublets: " << doublets << " (" << ratio * 100 << "%)\n";
            for (auto const& [rune, count] : doublet_map) {
                ss << "  Rune " << (int)rune << ": " << count << "x\n";
            }
            if (!runs.empty()) {
                ss << "  Runs distribution: ";
                std::map<int, int> run_freqs;
                for(int r : runs) run_freqs[r]++;
                for(auto const& [len, freq] : run_freqs) ss << len << "x" << freq << " ";
                ss << "\n";
            }
            ss << "------------------------------------------\n";
            std::lock_guard<std::mutex> lock(mtx);
            f << ss.str();
        }
        }
    };

    std::vector<std::thread> threads;
    size_t pages_per_thread = pages.size() / num_threads;
    for (int t = 0; t < num_threads; ++t) {
        size_t start = t * pages_per_thread;
        size_t end = (t == num_threads - 1) ? pages.size() : (t + 1) * pages_per_thread;
        threads.emplace_back(worker, start, end);
    }
    for (auto& t : threads) t.join();

    std::wcout << L"Doublet analysis saved to ../output/doublet_analysis.txt\n";
}

void export_doublet_data(const std::string& output_path) {
    // Ensure directory exists
    try {
        std::filesystem::path outp(output_path);
        if (outp.has_parent_path()) std::filesystem::create_directories(outp.parent_path());
    } catch(...) {}
    std::ofstream csv(output_path);
    if (!csv.is_open()) return;
    csv << "Page,Runes,DoubletCount,DoubletRatio,DoubletRuneCounts,RunLengths,BigramCounts\n";

    const auto& pages = core::G_PAGES;
    for (const auto& page : pages) {
        if (page.content.empty()) continue;
        core::ProcessedText pt(page.content);
        auto idx = pt.indices();

        int doublets = 0;
        std::map<uint8_t, int> doublet_map;
        std::vector<int> runs;
        int current_run = 1;

        for (size_t i = 1; i < idx.size(); ++i) {
            if (idx[i] < 29 && idx[i] == idx[i-1]) { doublets++; doublet_map[idx[i]]++; current_run++; }
            else { if (current_run > 1) runs.push_back(current_run); current_run = 1; }
        }
        if (current_run > 1) runs.push_back(current_run);

        double ratio = (pt.rune_count() > 0) ? static_cast<double>(doublets) / static_cast<double>(pt.rune_count()) : 0.0;

        // Bigram counts (only for runic indices)
        std::map<std::pair<int,int>, int> bigram_counts;
        std::vector<uint8_t> clean;
        for (auto v : idx) if (v < 29) clean.push_back(v);
        for (size_t i = 0; i + 1 < clean.size(); ++i) bigram_counts[{clean[i], clean[i+1]}]++;

        // Serialize maps as JSON-like string (no external dependency)
        std::ostringstream drs;
        drs << "{";
        bool first = true;
        for (auto const& [r, c] : doublet_map) { if (!first) drs << ";"; drs << (int)r << ":" << c; first = false; }
        drs << "}";

        std::ostringstream runs_s;
        for (size_t i = 0; i < runs.size(); ++i) { if (i) runs_s << ";"; runs_s << runs[i]; }

        std::ostringstream big_s;
        big_s << "{"; first = true;
        for (auto const& kv : bigram_counts) { if (!first) big_s << ";"; big_s << kv.first.first << "-" << kv.first.second << ":" << kv.second; first = false; }
        big_s << "}";

        // Escape commas in page name if any
    std::string pname(page.name);
        for (auto &ch : pname) if (ch == ',') ch = ';';

        csv << pname << "," << pt.rune_count() << "," << doublets << "," << std::fixed << std::setprecision(6) << ratio << ","
            << '"' << drs.str() << '"' << ","
            << '"' << runs_s.str() << '"' << ","
            << '"' << big_s.str() << '"' << "\n";
    }
    csv.close();
    std::wcout << L"Doublet CSV exported to " << std::wstring(output_path.begin(), output_path.end()) << L"\n";
}

void run_advanced_signal_analysis(int num_threads) {
    std::wcout << L"\n=== ADVANCED SIGNAL & DELTA PERIODICITY ===\n";
    std::ofstream f("../output/signal_periodicity.txt");
    std::mutex mtx;

    const auto& pages = core::G_PAGES;
    auto worker = [&](size_t start, size_t end) {
        for (size_t idx_p = start; idx_p < end; ++idx_p) {
            const auto& pg = pages[idx_p];
        std::vector<size_t> interrupts = core::get_possible_interrupters(pg.index);
        if (interrupts.size() < 10) continue;

        std::vector<long long> deltas;
        for (size_t j = 1; j < interrupts.size(); ++j) {
            deltas.push_back(static_cast<long long>(interrupts[j]) - interrupts[j-1]);
        }

        // Search for delta periodicity (Cyclic PRNG hypothesis)
        std::stringstream ss;
        for (size_t period = 2; period <= deltas.size() / 2; ++period) {
            int matches = 0;
            for (size_t i = 0; i < deltas.size() - period; ++i) {
                if (deltas[i] == deltas[i + period]) matches++;
            }
            double score = static_cast<double>(matches) / static_cast<double>(deltas.size() - period);
            
            if (score > 0.4) { // High repeat jump pattern
                ss << "Page " << pg.name << " | [!] STRONG DELTA PERIOD: " << period << " (Conf: " << score * 100 << "%)\n";
                ss << "      Pattern: ";
                for(size_t k=0; k<period; ++k) ss << deltas[k] << " ";
                ss << "\n";
            }
        }

        double sum = 0.0;
        for (auto d : deltas) sum += static_cast<double>(d);
        double mean = deltas.empty() ? 0.0 : sum / static_cast<double>(deltas.size());

        double var = 0.0;
        for (auto d : deltas) var += (static_cast<double>(d) - mean) * (static_cast<double>(d) - mean);
        double std_dev = deltas.empty() ? 0.0 : std::sqrt(var / static_cast<double>(deltas.size()));
        
        ss << "  Statistics: Mean Delta=" << mean << " StdDev=" << std_dev << "\n";
        if (std_dev < 1.0) ss << "  [SIGNAL] PAGE IS HIGHLY RHYTHMIC (POSSIBLE LINEAR PRNG)\n";
        else if (std_dev > 50.0) ss << "  [SIGNAL] PAGE IS HIGHLY VOLATILE (POSSIBLE NON-LINEAR OR MULTI-LAYER)\n";
        
        ss << "------------------------------------------\n";
        {
            std::lock_guard<std::mutex> lock(mtx);
            f << ss.str();
        }
        }
    };

    std::vector<std::thread> threads;
    size_t pages_per_thread = pages.size() / num_threads;
    for (int t = 0; t < num_threads; ++t) {
        size_t start = t * pages_per_thread;
        size_t end = (t == num_threads - 1) ? pages.size() : (t + 1) * pages_per_thread;
        threads.emplace_back(worker, start, end);
    }
    for (auto& t : threads) t.join();

    std::wcout << L"Signal analysis saved to ../output/signal_periodicity.txt\n";
}

void run_cross_page_pattern_analysis(size_t min_len) {
    std::wcout << L"\n=== CROSS-PAGE PATTERN CLUSTERING (Min Len: " << min_len << L") ===\n";
    std::ofstream f("../output/cross_page_patterns.txt");
    std::map<std::string, std::vector<std::string>> pattern_map;

    for (const auto& page : core::G_PAGES) {
        if (page.content.empty()) continue;
        core::ProcessedText pt(page.content);
        auto& clean = pt.indices(); // Using raw indices
        std::vector<uint8_t> runes;
        for(auto x : clean) if(x < 29) runes.push_back(x);

        if(runes.size() < min_len) continue;
        for (size_t i = 0; i <= runes.size() - min_len; ++i) {
            std::string seq;
            for(size_t k = 0; k < min_len; ++k) seq += static_cast<char>(runes[i+k]);
            pattern_map[seq].push_back(std::string(page.name));
        }
    }

    for (auto const& [seq, pages] : pattern_map) {
        std::set<std::string> unique_pages(pages.begin(), pages.end());
        if (unique_pages.size() > 1) {
            f << "Pattern [ ";
            for(auto x : seq) f << static_cast<int>(static_cast<uint8_t>(x)) << " ";
            f << "] found across " << unique_pages.size() << " pages: ";
            for(const auto& p : unique_pages) f << p << " ";
            f << "\n";
        }
    }
    std::wcout << L"Cross-page patterns saved to ../output/cross_page_patterns.txt\n";
}

void run_view_resolved_pages() {
    for (size_t i = 0; i < core::G_PAGES.size(); ++i) {
        const auto& pg = core::G_PAGES[i];
        if (pg.content.empty() || !core::has_known_solution(pg.index)) continue;
        core::ProcessedText pt(pg.content, i + 1);
        if (core::apply_known_solution(pg, pt)) {
            std::string latin = pt.to_latin();
            std::string_view snippet = utf8_take(latin, 300);
            std::wcout << GREEN_COLOR << L"[" << std::wstring(pg.name.begin(), pg.name.end()) << L"]" << RESET_COLOR << L"\n"
                       << std::wstring(snippet.begin(), snippet.end()) << L"...\n\n";
        }
    }
}

void run_transition_correlation_analysis() {
    std::wcout << L"\n=== RUNIC TRANSITION (MARKOV) ANALYSIS ===\n";
    std::vector<std::pair<std::string, std::vector<double>>> matrices;

    for (const auto& page : core::G_PAGES) {
        if (page.content.empty()) continue;
        core::ProcessedText pt(page.content);
        matrices.push_back({std::string(page.name), pt.transition_matrix()});
    }

    std::ofstream csv("../output/corr_markov.csv");
    csv << "Page";
    for (const auto& m : matrices) csv << "," << m.first;
    csv << "\n";

    for (const auto& m1 : matrices) {
        csv << m1.first;
        for (const auto& m2 : matrices) {
            double corr = calculate_correlation(m1.second, m2.second);
            csv << "," << std::fixed << std::setprecision(6) << corr;
        }
        csv << "\n";
    }
    csv.close();
    std::wcout << L"Markov Transition matrix saved to ../output/corr_markov.csv\n";
    std::wcout << L"Use the Python script to visualize hidden language structures.\n";
}

void run_kasiski_examination(size_t min_len) {
    std::wcout << L"\n=== KASISKI EXAMINATION (Periodic Pattern Search) ===\n";
    std::ofstream f("../output/kasiski_results.txt");

    for (const auto& page : core::G_PAGES) {
        if (page.content.empty() || core::has_known_solution(page.index)) continue;

        core::ProcessedText pt(page.content);
        const auto& indices = pt.indices();
        std::vector<uint8_t> runes;
        for (auto x : indices) if (x < 29) runes.push_back(x);

        if (runes.size() < 50) continue;

        std::map<std::string, std::vector<size_t>> sequences;
        for (size_t i = 0; i <= runes.size() - min_len; ++i) {
            std::string seq;
            for(size_t k = 0; k < min_len; ++k) seq += static_cast<char>(runes[i+k]);
            sequences[seq].push_back(i);
        }

        bool found_on_page = false;
        for (auto const& [seq, pos] : sequences) {
            if (pos.size() > 1) {
                if (!found_on_page) {
                    f << "Page: " << page.name << "\n";
                    found_on_page = true;
                }
                f << "  Pattern [ ";
                for (auto x : seq) f << static_cast<int>(static_cast<uint8_t>(x)) << " ";
                f << "] Positions: ";
                std::vector<long long> deltas;
                for (size_t i = 0; i < pos.size(); ++i) {
                    f << pos[i] << (i == pos.size() - 1 ? "" : ", ");
                    if (i > 0) deltas.push_back(pos[i] - pos[i - 1]);
                }
                f << " | GCD of deltas: " << gcd_vector(deltas) << "\n";
            }
        }
        if (found_on_page) f << "------------------------------------------\n";
    }
    std::wcout << L"Kasiski analysis saved to ../output/kasiski_results.txt\n";
}

void export_page_features(const std::string& filename) {
    std::ofstream f(filename);
        if (!f.is_open()) {
            std::wcerr << L"Error creating " << std::wstring(filename.begin(), filename.end()) << L"\n";
            return;
        }
    
    auto target_uni = calculate_liber_unigram_target();
    auto target_bi = calculate_liber_bigram_target();
    
    f << "Page,Length,IoC,Entropy,ChiSquare,Fitness,BigramIoC,NumInterrupts,GpsSumMean,TopDelta,LRS,LZ\n";
    
    for (const auto& page : core::G_PAGES) {
        if (page.content.empty()) continue;
        core::ProcessedText pt(page.content, page.index);
        
        auto dist = pt.runic_distribution();
        double chi = calculate_chi_square(dist, target_uni, pt.rune_count());
        double fit = score_text_fitness_advanced(pt, target_uni, target_bi);
        
        auto interrupts = core::get_possible_interrupters(page.index);
        int top_delta = 0;
        for (size_t i = 1; i < interrupts.size(); ++i) {
            top_delta = std::max(top_delta, static_cast<int>(interrupts[i] - interrupts[i-1]));
        }
        
        // Métrica LRS real em vez de apenas o pico da autocorrelação
        size_t lrs_len = pt.longest_repeated_substring_len();

        auto words = pt.get_words();
    double total_gps = 0.0;
    for (const auto& w : words) total_gps += pt.calculate_gp_sum(w);
    double avg_gps = words.empty() ? 0.0 : total_gps / static_cast<double>(words.size());

        f << page.name << "," 
          << pt.rune_count() << "," 
          << std::fixed << std::setprecision(5) << pt.index_of_coincidence() << "," 
          << pt.entropy() << "," 
          << chi << "," 
          << fit << ","
          << pt.bigram_index_of_coincidence() << ","
          << interrupts.size() << ","
          << avg_gps << ","
          << top_delta << ","
          << lrs_len << ","
          << pt.lempel_ziv_complexity() << "\n";
    }
    std::wcout << L"Features exported to " << std::wstring(filename.begin(), filename.end()) << L"\n";
}

void run_delta_stream_analysis() {
    std::wcout << L"\n=== DELTA STREAM ANALYSIS ===\n";
    std::ofstream txt("../output/delta_stream_analysis.txt");
    std::ofstream csv("../output/delta_stream_analysis.csv");

    csv << "Page,Position,Delta\n";

    for (const auto& pg : core::G_PAGES) {
        if (pg.content.empty()) continue;

        core::ProcessedText pt(pg.content);
        std::vector<uint8_t> runes;
        for (auto idx : pt.indices()) {
            if (idx < 29) runes.push_back(idx);
        }

        if (runes.size() < 2) continue;

        std::vector<uint8_t> deltas;
        std::array<size_t, 29> freq{};

        for (size_t i = 1; i < runes.size(); ++i) {
            uint8_t d = static_cast<uint8_t>(
                (static_cast<int>(runes[i]) - static_cast<int>(runes[i - 1]) + 29) % 29
            );
            deltas.push_back(d);
            freq[d]++;
            csv << pg.name << "," << (i - 1) << "," << static_cast<int>(d) << "\n";
        }

        double delta_ioc = calculate_ioc_from_indices(deltas);
        
        // Delta Entropy Calculation
        double entropy = 0.0;
        for (auto count : freq) {
            if (count > 0) {
                double p = static_cast<double>(count) / static_cast<double>(deltas.size());
                entropy -= p * std::log2(p);
            }
        }

        txt << "Page: " << pg.name << "\n"
            << "  Delta IoC:     " << std::fixed << std::setprecision(5) << delta_ioc << "\n"
            << "  Delta Entropy: " << std::fixed << std::setprecision(5) << entropy << "\n"
            << "  Deltas: ";

        for (size_t i = 0; i < std::min<size_t>(20, deltas.size()); ++i)
            txt << static_cast<int>(deltas[i]) << " ";

        if (deltas.size() > 20) txt << "...";
        txt << "\n------------------------------------------\n";
    }

    txt.close();
    csv.close();

    std::wcout << L"Delta stream saved to ../output/delta_stream_analysis.txt and .csv\n";
}

void run_delta_autocorrelation_analysis() {
    std::wcout << L"\n=== DELTA AUTOCORRELATION ANALYSIS ===\n";
    std::ofstream csv("../output/delta_autocorrelation.csv");
    csv << "Page,Lag,Score\n";

    for (const auto& pg : core::G_PAGES) {
        if (pg.content.empty()) continue;

        core::ProcessedText pt(pg.content);
        std::vector<uint8_t> runes;
        for (auto idx : pt.indices()) {
            if (idx < 29) runes.push_back(idx);
        }

        if (runes.size() < 2) continue;

        std::vector<uint8_t> deltas;
        for (size_t i = 1; i < (size_t)runes.size(); ++i) {
            deltas.push_back(static_cast<uint8_t>(
                (static_cast<int>(runes[i]) - static_cast<int>(runes[i - 1]) + 29) % 29
            ));
        }

        // Analyze lags from 1 up to 200 (or half page length)
        int max_lag = std::min<int>(200, static_cast<int>(deltas.size() / 2));
        for (int lag = 1; lag <= max_lag; ++lag) {
            int matches = 0;
            int total = 0;

            for (size_t i = 0; i + lag < deltas.size(); ++i) {
                if (deltas[i] == deltas[i + lag]) matches++;
                total++;
            }

            if (total > 0) {
                double score = static_cast<double>(matches) / static_cast<double>(total);
                csv << pg.name << "," << lag << "," << std::fixed << std::setprecision(5) << score << "\n";
            }
        }
    }
    csv.close();
    std::wcout << L"Delta autocorrelation saved to ../output/delta_autocorrelation.csv\n";
}

void run_cluster_analysis() {
    std::wcout << L"\n=== SECTIONAL STRUCTURE ANALYSIS (LIBER PRIMUS) ===\n";
    std::ofstream txt("../output/cluster_analysis.txt");
    if (!txt.is_open()) return;

    for (const auto& section : G_LIBER_SECTIONS) {
        std::wcout << L"Analyzing " << std::wstring(section.name.begin(), section.name.end()) << L"...\n";

        std::vector<uint8_t> merged_runes;
        std::vector<core::ProcessedText> processed_pages;

        txt << "==========================================================================\n";
        txt << "SECTION ANALYSIS: " << section.name << "\n";
        txt << "Pages: ";

        bool any_solved = false;
        std::string methods = "";
        // Using std::string for set keys to avoid compiler warnings and improve speed
        std::vector<std::set<std::string>> vocabularies;

        for (size_t p_num : section.page_indices) {
            const core::Page* pg = nullptr;
            for (const auto& p : core::G_PAGES) if (p.index == p_num) { pg = &p; break; }
            if (!pg || pg->content.empty()) continue;

            txt << "Page " << p_num << " ";
            core::ProcessedText pt(pg->content, pg->index);
            processed_pages.push_back(pt);

            // Collect words for Jaccard similarity
            auto words_vec = pt.get_words();
            std::set<std::string> word_set;
            for(const auto& w : words_vec) 
                word_set.insert(std::string(w.begin(), w.end()));
            vocabularies.push_back(std::move(word_set));

            // FILTER INTERRUPTERS: Only merge runes that are not known interrupters
            // This makes the Friedman Scan much more accurate.
            auto interrupters = core::get_possible_interrupters(p_num);
            std::set<size_t> int_set(interrupters.begin(), interrupters.end());
            size_t r_count = 0;
            for (auto r : pt.indices()) {
                if (r < 29) {
                    if (int_set.find(r_count) == int_set.end()) merged_runes.push_back(r);
                    r_count++;
                }
            }

            if (core::has_known_solution(p_num)) {
                any_solved = true;
                std::string m = core::get_solution_method(p_num);
                if (methods.find(m) == std::string::npos) {
                    if (!methods.empty()) methods += ", ";
                    methods += m;
                }
            }
        }
        txt << "\nStatus: " << (any_solved ? "RESOLVED/PARTIAL" : "UNRESOLVED") << "\n";
        if (!methods.empty()) txt << "Methods found: " << methods << "\n";
        
        if (merged_runes.empty()) { txt << "No content found for this section.\n\n"; continue; }

        // 1. IoC Rúnico (Bruto)
        double r_ioc = calculate_ioc_from_indices(merged_runes);

        // 2. IoC Latino e Entropia (Aplicando solução se disponível)
        double total_l_ioc = 0, total_ent = 0;
        for (size_t i = 0; i < processed_pages.size(); ++i) {
            core::ProcessedText pt_dec = processed_pages[i];
            size_t p_num = section.page_indices[i];
            const core::Page* pg_ptr = nullptr;
            for (const auto& p : core::G_PAGES) if (p.index == p_num) { pg_ptr = &p; break; }
            
            if (pg_ptr && core::has_known_solution(p_num)) core::apply_known_solution(*pg_ptr, pt_dec);
            total_l_ioc += pt_dec.latin_index_of_coincidence();
            total_ent += pt_dec.entropy();
        }
        double avg_l_ioc = total_l_ioc / static_cast<double>(processed_pages.size());
        double avg_ent = total_ent / static_cast<double>(processed_pages.size());

        txt << "\n[STATISTICAL SUMMARY]\n";
        txt << "  Total Runes: " << merged_runes.size() << "\n";
        txt << "  Runic IoC:   " << std::fixed << std::setprecision(5) << r_ioc << (r_ioc > 0.06 ? " (HIGH: Likely Mono)" : " (LOW: Likely Poly)") << "\n";
        txt << "  Latin IoC:   " << std::fixed << std::setprecision(5) << avg_l_ioc << (avg_l_ioc > 0.06 ? " (PLAINTEXT)" : " (CIPHERED)") << "\n";
        txt << "  Entropy:     " << std::fixed << std::setprecision(4) << avg_ent << " bits per rune\n";

        // 3. Internal Comparison Matrix (Enhanced)
        if (processed_pages.size() > 1) {
            txt << "\n[INTERNAL RELATIONSHIP MATRIX]\n";
            txt << "Legend: [U] Unigram Corr | [M] Markov Corr | [J] Vocabulary Jaccard Similarity\n\n      ";
            for(size_t p_num : section.page_indices) txt << "P" << std::setw(2) << p_num << "            ";
            txt << "\n";
            for (size_t i = 0; i < processed_pages.size(); ++i) {
                txt << "P" << std::setw(2) << section.page_indices[i] << " |";
                for (size_t j = 0; j < processed_pages.size(); ++j) {
                    double c_uni = calculate_correlation(processed_pages[i].runic_distribution(), processed_pages[j].runic_distribution());
                    double c_mar = calculate_correlation(processed_pages[i].transition_matrix(), processed_pages[j].transition_matrix());
                    
                    double jaccard = 0.0;
                    if (!vocabularies[i].empty() && !vocabularies[j].empty()) {
                        std::vector<std::string> intersect;
                        std::set_intersection(vocabularies[i].begin(), vocabularies[i].end(),
                                              vocabularies[j].begin(), vocabularies[j].end(),
                                              std::back_inserter(intersect));
                        jaccard = static_cast<double>(intersect.size()) / static_cast<double>(vocabularies[i].size() + vocabularies[j].size() - intersect.size());
                    }

                    txt << " U:" << std::fixed << std::setprecision(2) << c_uni 
                        << " M:" << c_mar 
                        << " J:" << jaccard << " |";
                }
                txt << "\n";
            }
            
            // Vocabulary Diversity Metric
            std::set<std::string> section_vocab;
            size_t total_words_count = 0;
            for(const auto& v : vocabularies) { 
                section_vocab.insert(v.begin(), v.end()); 
                total_words_count += v.size(); 
            }
            double vocab_unique_ratio = (total_words_count == 0) ? 0.0 : static_cast<double>(section_vocab.size()) / static_cast<double>(total_words_count);
            txt << "\n  Section Vocabulary Diversity: " << std::fixed << std::setprecision(3) << vocab_unique_ratio 
                << " (Low ratio means many repeated words across pages)\n";
        }

        // 4. Kasiski e Friedman Global no bloco da seção
        txt << "\n[PERIODICITY SCAN ON MERGED BLOCK]\n";
        std::map<std::string, std::vector<size_t>> sequences;
        size_t min_len = 4;
        for (size_t i = 0; i + min_len <= merged_runes.size(); ++i) {
            std::string seq;
            for(size_t k = 0; k < min_len; ++k) seq += static_cast<char>(merged_runes[i+k]);
            sequences[seq].push_back(i);
        }
        
        bool kasiski_found = false;
        for (auto const& [seq, pos] : sequences) {
            if (pos.size() > 2) {
                if (!kasiski_found) txt << "  Kasiski Hits (Multi-Page Repeats):\n";
                kasiski_found = true;
                txt << "    Pattern [ "; for (auto x : seq) txt << static_cast<int>(static_cast<uint8_t>(x)) << " ";
                txt << "] Distances GCD: ";
                std::vector<long long> deltas;
                for (size_t k = 1; k < pos.size(); ++k) deltas.push_back(pos[k] - pos[k - 1]);
                txt << gcd_vector(deltas) << "\n";
            }
        }

        txt << "  Friedman Scan (Best Lags):\n";
        for (int kl = 2; kl <= 120; ++kl) {
            if (static_cast<size_t>(kl) > merged_runes.size() / 2) break;
            std::vector<size_t> col_counts(kl, 0);
            std::vector<std::array<double, 29>> col_dists(kl, std::array<double, 29>{0.0});
            for (size_t i = 0; i < merged_runes.size(); ++i) { col_dists[i % kl][merged_runes[i]]++; col_counts[i % kl]++; }
            double avg_col_ioc = 0.0;
            for (int c = 0; c < kl; ++c) {
                if (col_counts[c] <= 1) continue;
                double sum = 0.0;
                double N = static_cast<double>(col_counts[c]);
                for (size_t k = 0; k < 29; ++k) sum += col_dists[c][k] * (col_dists[c][k] - 1.0);
                avg_col_ioc += sum / (N * (N - 1.0));
            }
            avg_col_ioc /= kl;
            if (avg_col_ioc > 0.045) txt << "    KL " << std::setw(3) << kl << ": IoC=" << std::fixed << std::setprecision(5) << avg_col_ioc << "\n";
        }
        txt << "\n\n";
    }

    txt.close();
    std::wcout << L"Sectional Analysis completed. See ../output/cluster_analysis.txt\n";
}

void run_auto_vigenere_solver() {
    std::wcout << L"\n=== AUTO-VIGENERE STATISTICAL SOLVER ===\n";
    auto target_dist = calculate_liber_unigram_target();

    std::wcout << L"Enter Page Index (1-74): ";
    size_t p_idx; std::cin >> p_idx;
    if (p_idx < 1 || p_idx > core::G_PAGES.size()) return;
    const auto& pg = core::G_PAGES[p_idx - 1];

    std::wcout << L"Enter suspected Key Length (or 0 to auto-detect best KL 2-100): ";
    int kl; std::cin >> kl;

    core::ProcessedText pt_orig(pg.content);

    auto interrupts_vec = core::get_possible_interrupters(p_idx);
    std::set<size_t> interrupt_set(interrupts_vec.begin(), interrupts_vec.end());

    std::vector<uint8_t> clean_runes;
    size_t r_count = 0;
    for (auto idx : pt_orig.indices()) {
        if (idx < 29) {
            if (interrupt_set.find(r_count) == interrupt_set.end()) {
                clean_runes.push_back(idx);
            }
            r_count++;
        }
    }

    if (clean_runes.empty()) { std::wcout << L"No valid runes (non-interrupts) found.\n"; return; }

    // Auto-detection if KL is 0
    if (kl <= 0) {
        double best_avg_ioc = 0;
        int best_kl = 1;
        // Note: periodic_ioc should be called on a ProcessedText built from clean_runes
        core::ProcessedText pt_clean("");
        pt_clean.indices() = clean_runes;

        for (int test_kl = 2; test_kl <= 100 && test_kl < (int)clean_runes.size()/2; ++test_kl) {
            double current_ioc = pt_clean.periodic_ioc(test_kl);
            if (current_ioc > best_avg_ioc) {
                best_avg_ioc = current_ioc;
                best_kl = test_kl;
            }
        }
        std::wcout << L"[AUTO] Best detected Key Length: " << best_kl 
                   << L" (Avg Col IoC: " << best_avg_ioc << L")\n";
        kl = best_kl;
    }

    std::vector<int> discovered_key;
    std::wcout << L"Solving columns...\n";

    for (int col = 0; col < kl; ++col) {
        std::vector<uint8_t> column_runes;
        for (size_t i = col; i < clean_runes.size(); i += kl) {
            column_runes.push_back(clean_runes[i]);
        }

        int best_shift = 0;
        double best_fit = -1.0;

        // Test each of 29 possible shifts for this column
        for (int s = 0; s < 29; ++s) {
            std::array<double, 29> col_dist{0.0};
            for (auto r : column_runes) {
                int decrypted_rune = (static_cast<int>(r) - s + 29) % 29;
                col_dist[decrypted_rune]++;
            }
            for (double& d : col_dist) d /= static_cast<double>(column_runes.size());

            // Measure column fitness using book Unigram Target
            double current_fit = 0.0;
            for (size_t i = 0; i < 29; ++i) {
                // simple dot product/correlation for columns
                current_fit += col_dist[i] * target_dist[i];
            }

            if (current_fit > best_fit) {
                best_fit = current_fit;
                best_shift = s;
            }
        }
        discovered_key.push_back(best_shift);
    }

    // Display Deduced Keystream
    std::wcout << GREEN_COLOR << L"\n[DEDUCED KEYSTREAM]: " << RESET_COLOR;
    for (int k : discovered_key) std::wcout << k << L" ";
    std::wcout << L"\n";

    // Convert numeric keystream to human-readable latin and rune strings
    std::string deduced_rune_key;
    std::string deduced_latin_key;
    for (int k : discovered_key) {
        if (k >= 0 && k < static_cast<int>(core::RUNE_TABLE.size())) {
            deduced_rune_key += core::RUNE_TABLE[k].rune;
            deduced_latin_key += std::string(core::RUNE_TABLE[k].latin);
        }
    }

    if (!deduced_latin_key.empty()) {
        std::wcout << L"[DEDUCED KEY (latin)]: " << std::wstring(deduced_latin_key.begin(), deduced_latin_key.end()) << L"\n";
    }
    if (!deduced_rune_key.empty()) {
        core::ProcessedText pk(deduced_rune_key);
        std::string latin_from_runes = pk.to_latin();
        if (!latin_from_runes.empty()) {
            std::wcout << L"[DEDUCED KEY (runes -> latin)]: " << std::wstring(latin_from_runes.begin(), latin_from_runes.end()) << L"\n";
        } else {
            std::wcout << L"[DEDUCED KEY (runes)]: " << std::wstring(deduced_rune_key.begin(), deduced_rune_key.end()) << L"\n";
        }
    }

    // Attempt partial translation
    std::vector<uint8_t> result_indices = pt_orig.indices();
    size_t r_pos = 0;
    for (auto& idx : result_indices) {
        if (idx < 29) {
            int key_val = discovered_key[r_pos % kl];
            idx = static_cast<uint8_t>((static_cast<int>(idx) - key_val + 29) % 29);
            r_pos++;
        }
    }

    // Apply numeric keystream directly (as before) to produce a baseline preview
    core::ProcessedText solved_pt("", pg.index);
    solved_pt.indices() = result_indices;
    std::string latin = solved_pt.to_latin();

    std::wcout << L"\nPreview with numeric keystream applied:\n"
               << std::wstring(latin.begin(), latin.end()).substr(0, 500) << L"...\n";

    // Also test using the deduced rune key and patched latin key as Vigenere keys
    std::vector<std::pair<std::string, core::ProcessedText>> candidates;

    if (!deduced_rune_key.empty()) {
        core::ProcessedText pt_rune = pt_orig;
        core::VigenereTransformer vt_rune(deduced_rune_key);
        vt_rune.transform(pt_rune);
        candidates.emplace_back(deduced_rune_key, std::move(pt_rune));
    }

    if (!deduced_latin_key.empty()) {
        // try latin key converted to runes (if possible)
        auto maybe_runes = core::to_runes(deduced_latin_key);
        std::string as_runes = maybe_runes ? *maybe_runes : deduced_latin_key;
        core::ProcessedText pt_lat = pt_orig;
        core::VigenereTransformer vt_lat(as_runes);
        vt_lat.transform(pt_lat);
        candidates.emplace_back(as_runes, std::move(pt_lat));

        // Also try patched latin variant (user heuristic)
        std::string patched = core::patch_key(deduced_latin_key);
        if (!patched.empty() && patched != deduced_latin_key) {
            auto maybe_r = core::to_runes(patched);
            std::string patched_runes = maybe_r ? *maybe_r : patched;
            core::ProcessedText pt_p = pt_orig;
            core::VigenereTransformer vt_p(patched_runes);
            vt_p.transform(pt_p);
            candidates.emplace_back(patched_runes, std::move(pt_p));
        }
    }

    std::ofstream out("../output/auto_solver_result.txt", std::ios::app);
    out << "Page: " << pg.name << " | KeyLen: " << kl << "\nKey (nums): ";
    for (int k : discovered_key) out << k << " ";
    out << "\nKey (latin): " << deduced_latin_key << "\nKey (runes): " << deduced_rune_key << "\n";

    // Evaluate candidate transforms and write hits with fitness
    for (auto &cand : candidates) {
        double fit = score_text_fitness(cand.second, target_dist);
        std::string latin_preview = cand.second.to_latin();
        out << "Candidate Key: " << cand.first << " | Fit: " << fit << "\n";
        out << "Preview: " << latin_preview.substr(0, 1000) << "\n---\n";
        if (fit > 0.9) {
            std::wcout << GREEN_COLOR << L"[CANDIDATE HIT] Page: " << std::wstring(pg.name.begin(), pg.name.end())
                       << L" | Key: " << std::wstring(cand.first.begin(), cand.first.end()) << L" | Fit: " << fit << RESET_COLOR << L"\n";
        }
    }

    out << "Text (numeric preview): " << latin << "\n\n";
    out.close();
}

void run_manual_vigenere_attack(const std::string& key,
                                const std::vector<size_t>& target_pages,
                                const std::vector<size_t>& interrupt_positions,
                                double fitness_threshold,
                                const std::string& output_file)
{
    std::ofstream out(output_file, std::ios::app);
    if (!out.is_open()) return;

    auto target_dist = calculate_liber_unigram_target();

    // Prepare key: accept Latin text or runes
    auto maybe_runes = core::to_runes(key);
    std::string runes_str = maybe_runes ? *maybe_runes : key;

    // Also compute patched key variant (user's idea)
    std::string patched = core::patch_key(key);

    for (size_t pidx : target_pages) {
        if (pidx < 1 || pidx > core::G_PAGES.size()) continue;
        const auto& pg = core::G_PAGES[pidx - 1];
        if (pg.content.empty()) continue;

        core::ProcessedText pt(pg.content, pg.index);
        std::set<size_t> int_set(interrupt_positions.begin(), interrupt_positions.end());

        // Try original key and patched key (if different)
        std::vector<std::string> candidates = { runes_str };
        if (!patched.empty() && patched != key) {
            candidates.push_back(patched);
        }

        for (const auto& candidate_key : candidates) {
            // VigenereTransformer expects utf8 runes string; we pass candidate_key as-is
            core::ProcessedText pt_copy = pt;
            core::VigenereTransformer vt(candidate_key);
            vt.transform(pt_copy);

            double fit = score_text_fitness(pt_copy, target_dist);
            std::string full_latin = pt_copy.to_latin();

            if (fit >= fitness_threshold) {
                out << "=== MANUAL VIGENERE HIT ===\n";
                
                // Improved display key logic to avoid "???"
                std::string display_key = key; 
                if (candidate_key == patched) display_key = patched + " (PATCHED)";

                std::wstring wpage(pg.name.begin(), pg.name.end());
                std::wstring wkey(display_key.begin(), display_key.end());
                std::wstring wpreview(full_latin.begin(), full_latin.end());

                out << "Page: " << pg.name << " | Key: " << display_key << " | Fit: " << fit << "\n";
                out << "Preview: " << full_latin.substr(0, 800) << "\n\n";

                std::wcout << GREEN_COLOR << L"\n[MANUAL HIT] Fit: " << std::fixed << std::setprecision(5) << fit 
                           << L" | Page: " << wpage << L" | Key: " << wkey << RESET_COLOR << L"\n"
                           << L"Decrypted Content (Snippet):\n" 
                           << (wpreview.size() > 800 ? wpreview.substr(0, 800) : wpreview) << L"...\n"
                           << L"---------------------------------------------------\n";
            }
        }
    }
    out.close();
}

void run_gematria_sum_analysis() {
    std::wcout << L"\n=== GEMATRIA SUM ANALYSIS (Word-level Numerology) ===\n";
    std::ofstream f("../output/gematria_sums.txt");
    std::ofstream csv("../output/gematria_sums.csv");
    csv << "Page,WordIndex,Sum,IsPrime,IsFibonacci,IsLucas,IsSquare,DigitalRoot\n";

    for (const auto& pg : core::G_PAGES) {
        if (pg.content.empty()) continue;

        core::ProcessedText pt(pg.content, pg.index);
        auto words = pt.get_words();
        if (words.empty()) continue;

        f << "Page: " << pg.name << "\n";
        size_t word_idx = 0;
        std::vector<int> sums;
        
        int fib_count = 0;
        int sq_count = 0;
        int prime_count = 0;
        int lucas_count = 0;

        auto is_fib = [](int n) {
            auto is_perfect_sq = [](long long x) { long long s = static_cast<long long>(std::sqrt(static_cast<double>(x))); return s * s == x; };
            return is_perfect_sq(5LL*n*n + 4) || is_perfect_sq(5LL*n*n - 4);
        };
        auto is_lucas = [](int n) {
            auto is_perfect_sq = [](long long x) { long long s = static_cast<long long>(std::sqrt(static_cast<double>(x))); return s * s == x; };
            return is_perfect_sq(5LL*n*n + 20) || is_perfect_sq(5LL*n*n - 20);
        };
        auto is_sq = [](int n) { int s = static_cast<int>(std::sqrt(static_cast<double>(n))); return s * s == n; };

        for (const auto& word : words) {
            int sum = 0;
            for (uint8_t rune_idx : word) {
                // Use prime value defined in RUNE_TABLE
                sum += core::RUNE_TABLE[rune_idx].prime;
            }
            sums.push_back(sum);

            int digital_root = 1 + ((sum - 1) % 9);
            bool prime_sum = core::is_prime(sum);
            bool fib_sum = is_fib(sum);
            bool luc_sum = is_lucas(sum);
            bool sq_sum = is_sq(sum);
            
            if(prime_sum) prime_count++;
            if(fib_sum) fib_count++;
            if(luc_sum) lucas_count++;
            if(sq_sum) sq_count++;

            csv << pg.name << "," << word_idx << "," << sum << "," 
                << (prime_sum ? "1" : "0") << "," << (fib_sum ? "1" : "0") << ","
                << (luc_sum ? "1" : "0") << "," << (sq_sum ? "1" : "0") << "," << digital_root << "\n";
            
            word_idx++;
        }

        // Statistical analysis of page sums
        double avg = sums.empty() ? 0.0 : std::accumulate(sums.begin(), sums.end(), 0.0) / static_cast<double>(sums.size());

        f << "  Words: " << sums.size() << "\n"
          << "  Average Sum: " << std::fixed << std::setprecision(2) << avg << "\n"
          << "  Prime Sums:  " << prime_count << " (" << (sums.empty() ? 0.0 : 100.0 * static_cast<double>(prime_count) / static_cast<double>(sums.size())) << "%)\n"
          << "  Fibonacci:   " << fib_count << "\n"
          << "  Lucas Num:   " << lucas_count << "\n"
          << "  Squares:     " << sq_count << "\n";

        // Check for Arithmetic Progressions in sums (common in transposition ciphers)
        if (sums.size() >= 4) {
            std::map<int, int> delta_freq;
            for(size_t i=1; i<sums.size(); ++i) delta_freq[sums[i] - sums[i-1]]++;
            for(auto const& [d, count] : delta_freq) {
                if (count > (int)sums.size() / 4) f << "  [!] COMMON SUM DELTA DETECTED: " << d << " (" << count << "x)\n";
            }
        }
        
        // Check if sums are multiples of a common number (GCD)
        if (sums.size() > 1) {
            int common_gcd = sums[0];
            for(size_t i=1; i<sums.size(); ++i) common_gcd = std::gcd(common_gcd, sums[i]);
            if (common_gcd > 1) f << "  [!] ALL WORDS ARE MULTIPLES OF: " << common_gcd << "\n";
        }
        f << "------------------------------------------\n";
    }

    f.close();
    csv.close();
    std::wcout << L"Gematria analysis saved to ../output/gematria_sums.txt\n";
}

void run_multi_metric_clustering() {
    std::wcout << L"\n=== MULTI-METRIC CLUSTER GROUPING (Max 2 clusters per page) ===\n";
    std::ofstream f("../output/probable_clusters_report.txt");
    if (!f.is_open()) return;

    struct PageData {
        size_t index;
        std::string name;
        double ioc;
        double entropy;
        std::array<double, 29> runic_dist;
        std::vector<double> transition_mat;
    };

    std::vector<PageData> all_data;
    for (const auto& pg : core::G_PAGES) {
        if (pg.content.empty()) continue;
        core::ProcessedText pt(pg.content, pg.index);
        all_data.push_back({
            pg.index,
            std::string(pg.name),
            pt.index_of_coincidence(),
            pt.entropy(),
            pt.runic_distribution(),
            pt.transition_matrix()
        });
    }

    struct Edge { size_t u, v; double sim; };
    std::vector<Edge> edges;
    for (size_t i = 0; i < all_data.size(); ++i) {
        for (size_t j = i + 1; j < all_data.size(); ++j) {
            const auto& a = all_data[i];
            const auto& b = all_data[j];

            double c_uni = calculate_correlation(a.runic_dist, b.runic_dist);
            double c_mar = calculate_correlation(a.transition_mat, b.transition_mat);
            double i_sim = std::exp(-std::abs(a.ioc - b.ioc) / 0.005);
            double e_sim = std::exp(-std::abs(a.entropy - b.entropy) / 0.1);

            // Weighted similarity: Unigram (40%), Markov (30%), IoC (15%), Entropy (15%)
            double total_sim = (c_uni * 0.4) + (c_mar * 0.3) + (i_sim * 0.15) + (e_sim * 0.15);
            if (total_sim > 0.82) { 
                edges.push_back({i, j, total_sim});
            }
        }
    }
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) { return a.sim > b.sim; });

    std::vector<std::set<size_t>> clusters;
    std::map<size_t, int> participation;

    for (const auto& edge : edges) {
        int cluster_idx = -1;
        for (size_t i = 0; i < clusters.size(); ++i) {
            if (clusters[i].count(edge.u) || clusters[i].count(edge.v)) {
                cluster_idx = (int)i;
                break;
            }
        }

        if (cluster_idx != -1) {
            size_t node_to_add = clusters[cluster_idx].count(edge.u) ? edge.v : edge.u;
            if (participation[node_to_add] < 2 && !clusters[cluster_idx].count(node_to_add)) {
                clusters[cluster_idx].insert(node_to_add);
                participation[node_to_add]++;
            }
        } else if (participation[edge.u] < 2 && participation[edge.v] < 2) {
            std::set<size_t> new_cluster;
            new_cluster.insert(edge.u);
            new_cluster.insert(edge.v);
            clusters.push_back(new_cluster);
            participation[edge.u]++;
            participation[edge.v]++;
        }
    }

    f << "=== PROBABLE PAGE CLUSTERS REPORT ===\n";
    f << "Generated based on Multi-Metric Similarity (Correlation, IoC, Entropy)\n";
    f << "Constraint: A page can belong to at most 2 clusters.\n\n";

    for (size_t i = 0; i < clusters.size(); ++i) {
        if (clusters[i].size() < 2) continue;
        f << "--- Cluster #" << i + 1 << " (" << clusters[i].size() << " pages) ---\n";
        for (size_t idx : clusters[i]) {
            const auto& d = all_data[idx];
            f << "  Page: " << std::left << std::setw(10) << d.name 
              << " | IoC: " << std::fixed << std::setprecision(4) << d.ioc 
              << " | Entropy: " << d.entropy << " bits\n";
        }
        f << "\n";
    }

    f.close();
    std::wcout << L"Probable clusters report saved: ../output/probable_clusters_report.txt\n";
}

void run_vocabulary_overlap_analysis(const std::string& output_csv) {
    std::wcout << L"\n=== VOCABULARY OVERLAP ANALYSIS (Jaccard Similarity) ===\n";
    std::ofstream csv(output_csv);
    if (!csv.is_open()) return;

    struct PageVocab {
        std::string name;
        std::set<std::string> words;
    };

    std::vector<PageVocab> all_vocabs;
    for (const auto& pg : core::G_PAGES) {
        if (pg.content.empty()) continue;
        core::ProcessedText pt(pg.content, pg.index);
        auto words_vec = pt.get_words();
        std::set<std::string> word_set;
        for(const auto& w : words_vec) word_set.insert(std::string(w.begin(), w.end()));
        all_vocabs.push_back({std::string(pg.name), std::move(word_set)});
    }

    csv << "Page";
    for (const auto& pv : all_vocabs) csv << "," << pv.name;
    csv << "\n";

    for (const auto& pv1 : all_vocabs) {
        csv << pv1.name;
        for (const auto& pv2 : all_vocabs) {
            if (pv1.words.empty() || pv2.words.empty()) {
                csv << ",0.0";
                continue;
            }

            std::vector<std::string> intersect;
            std::set_intersection(pv1.words.begin(), pv1.words.end(),
                                  pv2.words.begin(), pv2.words.end(),
                                  std::back_inserter(intersect));
            
            double jaccard = static_cast<double>(intersect.size()) / 
                             static_cast<double>(pv1.words.size() + pv2.words.size() - intersect.size());
            csv << "," << std::fixed << std::setprecision(5) << jaccard;
        }
        csv << "\n";
    }
    csv.close();
    std::wcout << L"Vocabulary overlap matrix saved to: " << std::wstring(output_csv.begin(), output_csv.end()) << L"\n";
}

void run_structural_segment_analysis(const std::string& output_csv) {
    std::wcout << L"\n=== STRUCTURAL SEGMENT ANALYSIS ===\n";
    std::ofstream csv(output_csv);
    if (!csv.is_open()) return;

    csv << "Page,Type,Length\n";

    for (const auto& pg : core::G_PAGES) {
        if (pg.content.empty()) continue;
        
        core::ProcessedText pt(pg.content, pg.index);
        auto& indices = pt.indices();

        // 1. Comprimento de Palavras
        auto words = pt.get_words();
        for (const auto& w : words) {
            csv << pg.name << ",Word," << w.size() << "\n";
        }

        // 2. Comprimento de Linhas (distância entre SPECIAL_SLASH_IDX ou Newline)
        size_t current_line_len = 0;
        for (uint8_t idx : indices) {
            if (idx < 29) {
                current_line_len++;
            } else if (idx == core::SPECIAL_SLASH_IDX || idx == core::SPECIAL_NEWLINE_IDX) {
                if (current_line_len > 0) {
                    csv << pg.name << ",Line," << current_line_len << "\n";
                    current_line_len = 0;
                }
            }
        }
        if (current_line_len > 0) csv << pg.name << ",Line," << current_line_len << "\n";

        // 3. Comprimento de Cláusulas (SPECIAL_PERIOD_IDX)
        size_t current_clause_len = 0;
        for (uint8_t idx : indices) {
            if (idx < 29) {
                current_clause_len++;
            } else if (idx == core::SPECIAL_PERIOD_IDX) {
                if (current_clause_len > 0) {
                    csv << pg.name << ",Clause," << current_clause_len << "\n";
                    current_clause_len = 0;
                }
            }
        }
    }
    csv.close();
    std::wcout << L"Structural segments exported to: " << std::wstring(output_csv.begin(), output_csv.end()) << L"\n";
}

void run_mathematical_direction_search(int num_threads) {
    (void)num_threads; // Silencia o aviso de parâmetro não utilizado
    std::wcout << L"\n=== MATHEMATICAL DIRECTION SEARCH (Numbers are the direction) ===\n";
    auto target_dist = calculate_liber_unigram_target();
    std::ofstream f("../output/math_direction_hits.txt", std::ios::app);

    for (const auto& pg : core::G_PAGES) {
        if (pg.content.empty() || core::has_known_solution(pg.index)) continue;

        core::ProcessedText pt_orig(pg.content, pg.index);
        const auto& indices = pt_orig.indices();

        std::vector<double> offset_scores;
        std::vector<std::string> previews;
        
        // Teste 1: Keystream baseado na Função Totiente do Índice da Runa
        // K[i] = phi(word_index + offset) % 29
        for (int offset = 0; offset < 100; ++offset) {
            std::vector<uint8_t> test = indices;
            int rune_count = 0;
            for (auto& idx : test) {
                if (idx < 29) {
                    int shift = core::euler_totient(rune_count + offset) % 29;
                    idx = static_cast<uint8_t>((idx - shift + 29) % 29);
                    rune_count++;
                }
            }
            core::ProcessedText pt_res("", pg.index);
            pt_res.indices() = test;
            double fit = score_text_fitness(pt_res, target_dist);
            offset_scores.push_back(fit);
            previews.push_back(pt_res.to_latin().substr(0, 100));
        }

        // Cálculo de estatísticas para detecção de picos (Z-Score)
        double sum = std::accumulate(offset_scores.begin(), offset_scores.end(), 0.0);
        double mean = sum / static_cast<double>(offset_scores.size());
        double sq_sum = std::inner_product(offset_scores.begin(), offset_scores.end(), offset_scores.begin(), 0.0);
        double stdev = std::sqrt(sq_sum / static_cast<double>(offset_scores.size()) - mean * mean);

        for (size_t offset = 0; offset < offset_scores.size(); ++offset) {
            double z_score = (offset_scores[offset] - mean) / (stdev + 0.00001);
            
            // Um Z-Score > 3.0 indica um outlier estatístico (um pico real)
            if (z_score > 3.5 && offset_scores[offset] > 0.90) {
                f << "!!! REAL PEAK DETECTED !!!\n";
                f << "HIT [Totient Index] | Page: " << pg.name << " | Offset: " << offset 
                  << " | Fitness: " << offset_scores[offset] << " | Z-Score: " << z_score << "\n";
                f << "Text: " << previews[offset] << "...\n---\n";
            }
        }
        // Teste 2: Keystream baseado na Soma de Gematria das palavras
        // A "direção" pode ser a soma da palavra anterior influenciando a próxima
        auto words = pt_orig.get_words();
        std::vector<int> word_sums;
        for (const auto& w : words) {
            int s = 0;
            for (uint8_t r : w) s += core::RUNE_TABLE[r].prime;
            word_sums.push_back(s);
        }

        if (!word_sums.empty()) {
            // Tentar usar o Totiente da soma da palavra como shift para a própria palavra
            std::vector<uint8_t> test_word_shift = indices;
            size_t current_word_idx = 0;
            for (size_t i = 0; i < test_word_shift.size(); ++i) {
                if (test_word_shift[i] < 29) {
                    int shift = core::euler_totient(word_sums[current_word_idx]) % 29;
                    test_word_shift[i] = static_cast<uint8_t>((test_word_shift[i] - shift + 29) % 29);
                } else if (test_word_shift[i] == core::SPECIAL_HYPHEN_IDX || test_word_shift[i] == core::SPECIAL_SPACE_IDX) {
                    if (current_word_idx < word_sums.size() - 1) current_word_idx++;
                }
            }
            core::ProcessedText pt_word_res("", pg.index);
            pt_word_res.indices() = test_word_shift;
            if (score_text_fitness(pt_word_res, target_dist) > 0.85) {
                f << "HIT [Word Sum Totient] | Page: " << pg.name << "\n";
                f << "Text: " << pt_word_res.to_latin().substr(0, 200) << "\n---\n";
            }
        }
    }
    std::wcout << L"Mathematical search finished. Results in ../output/math_direction_hits.txt\n";
}

void run_merged_corpus_analysis(size_t start_page, size_t end_page) {
    std::wcout << L"\n=== MERGED CORPUS ANALYSIS (Pages " << start_page << L" to " << end_page << L") ===\n";
    
    std::vector<uint8_t> merged_runes;
    for (size_t i = start_page; i <= end_page; ++i) {
        for (const auto& pg : core::G_PAGES) {
            if (pg.index == i) {
                core::ProcessedText pt(pg.content);
                for (auto r : pt.indices()) if (r < 29) merged_runes.push_back(r);
            }
        }
    }

    if (merged_runes.empty()) return;

    core::ProcessedText pt_merged("");
    pt_merged.indices() = merged_runes;

    std::wcout << L"Total Runes: " << merged_runes.size() << L"\n";
    std::wcout << L"Global IoC: " << pt_merged.index_of_coincidence() << L"\n";
    std::wcout << L"Global Entropy: " << pt_merged.entropy() << L"\n";

    // Kasiski em larga escala
    auto repeats = pt_merged.kasiski_examination(5);
    std::wcout << L"Long repeats (>5 runes) found: " << repeats.size() << L"\n";
    
    std::ofstream f("../output/merged_corpus_report.txt");
    f << "MERGED CORPUS " << start_page << "-" << end_page << "\n";
    f << "Size: " << merged_runes.size() << " runes\n\n";
    
    for (auto const& [seq, pos] : repeats) {
        if (pos.size() > 2) {
            f << "Pattern: "; for(auto x : seq) f << (int)x << " ";
            f << " | Found " << pos.size() << " times. Distances: ";
            for(size_t k=1; k<pos.size(); ++k) f << (pos[k]-pos[k-1]) << " ";
            f << "\n";
        }
    }
    f.close();
    std::wcout << L"Report saved to ../output/merged_corpus_report.txt\n";
}

void run_route_transposition_search(const std::vector<size_t>& target_pages) {
    std::wcout << L"\n=== ROUTE TRANSPOSITION SEARCH (Scytale/Grid/Primes) ===\n";
    
    std::vector<int> candidate_widths = {17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73};
    auto target_dist = calculate_liber_unigram_target();

    for (size_t p_idx : target_pages) {
        const core::Page* pg = nullptr;
        for (const auto& p : core::G_PAGES) if (p.index == p_idx) pg = &p;
        if (!pg || pg->content.empty()) continue;

        core::ProcessedText pt_orig(pg->content);
        std::vector<uint8_t> runes;
        for (auto r : pt_orig.indices()) if (r < 29) runes.push_back(r);
        
        if (runes.size() < 40) continue;

        for (int w : candidate_widths) {
            if (static_cast<size_t>(w) >= runes.size()) continue;

            int rows = (static_cast<int>(runes.size()) + w - 1) / w;
            std::vector<std::vector<uint8_t>> grid(rows, std::vector<uint8_t>(w, 255)); // 255 = padding

            // Preenche a grade (Row-major)
            for (size_t i = 0; i < runes.size(); ++i) {
                grid[i / w][i % w] = runes[i];
            }

            // Testar Rotas
            auto test_route = [&](const std::vector<uint8_t>& reordered, const std::string& label) {
                core::ProcessedText pt_test("");
                pt_test.indices() = reordered;
                double ioc = pt_test.index_of_coincidence();
                
                // Se o IoC subir para perto de 0.045-0.050, temos algo
                if (ioc > 0.048) {
                    std::wcout << GREEN_COLOR << L"[POTENTIAL ROUTE] " << RESET_COLOR 
                               << L"Page: " << p_idx << L" | Width: " << w 
                               << L" | Route: " << std::wstring(label.begin(), label.end())
                               << L" | IoC: " << ioc << L"\n";
                    
                    // Testar se um Atbash simples sobre a rota resolve
                    for(int s=0; s<29; ++s) {
                        core::ProcessedText pt_atbash = pt_test;
                        core::AtbashTransformer(s).transform(pt_atbash);
                        if (score_text_fitness(pt_atbash, target_dist) > 0.90) {
                            std::wcout << GREEN_COLOR << L"  [!!!] HIT WITH ATBASH+" << s << L" ON THIS ROUTE!\n" << RESET_COLOR;
                            std::wcout << L"  Preview: " << std::wstring(pt_atbash.to_latin().begin(), pt_atbash.to_latin().end()).substr(0,100) << L"\n";
                        }
                    }
                }
            };

            // 1. Column-major (Scytale)
            std::vector<uint8_t> col_major;
            for (int c = 0; c < w; ++c) {
                for (int r = 0; r < rows; ++r) {
                    if (grid[r][c] != 255) col_major.push_back(grid[r][c]);
                }
            }
            test_route(col_major, "Column-Major");

            // 2. Boustrophedon (Serpente)
            std::vector<uint8_t> boustrophedon;
            for (int r = 0; r < rows; ++r) {
                if (r % 2 == 0) {
                    for (int c = 0; c < w; ++c) if (grid[r][c] != 255) boustrophedon.push_back(grid[r][c]);
                } else {
                    for (int c = w - 1; c >= 0; --c) if (grid[r][c] != 255) boustrophedon.push_back(grid[r][c]);
                }
            }
            test_route(boustrophedon, "Boustrophedon");

            // 3. Simple Diagonal (Zig-zag)
            std::vector<uint8_t> diagonal;
            for (int k = 0; k < rows + w - 1; ++k) {
                for (int j = 0; j <= k; ++j) {
                    int r = j;
                    int c = k - j;
                    if (r < rows && c < w && grid[r][c] != 255) diagonal.push_back(grid[r][c]);
                }
            }
            test_route(diagonal, "Diagonal");
            
            // 4. Inward Spiral (Espiral)
            std::vector<uint8_t> spiral;
            int top = 0, bottom = rows - 1, left = 0, right = w - 1;
            while (top <= bottom && left <= right) {
                for (int i = left; i <= right; ++i) if(grid[top][i] != 255) spiral.push_back(grid[top][i]);
                top++;
                for (int i = top; i <= bottom; ++i) if(grid[i][right] != 255) spiral.push_back(grid[i][right]);
                right--;
                if (top <= bottom) {
                    for (int i = right; i >= left; --i) if(grid[bottom][i] != 255) spiral.push_back(grid[bottom][i]);
                    bottom--;
                }
                if (left <= right) {
                    for (int i = bottom; i >= top; --i) if(grid[i][left] != 255) spiral.push_back(grid[i][left]);
                    left++;
                }
            }
            test_route(spiral, "Spiral");
        }
    }
}

void run_secrets_analysis() {
    std::wcout << L"\n=== DEEP SECRETS ANALYSIS ===\n";

    // 0. Análise do DEEP_HASH (Distribuição de Nibbles)
    std::ofstream hash_csv("../output/deep_hash_distribution.csv");
    hash_csv << "Nibble,Count\n";
    std::array<int, 16> nibbles{0};
    for(char c : core::secrets::DEEP_HASH) {
        int val = -1;
        if(isdigit(c)) val = c - '0';
        else if(c >= 'a' && c <= 'f') val = c - 'a' + 10;
        if(val != -1) nibbles[val]++;
    }
    for(int i=0; i<16; ++i) {
        hash_csv << std::hex << i << "," << std::dec << nibbles[i] << "\n";
    }
    hash_csv.close();
    std::wcout << L"  - Deep Hash distribution saved.\n";
    
    // 1. Exportação do Stream Cuneiforme (Base60)
    std::ofstream cun_csv("../output/cuneiform_stream.csv");
    cun_csv << "Index,Raw,Decimal,RuneIndex\n";
    auto b60_to_dec = [](std::string_view s) -> int {
        auto val = [](char c) -> int {
            if (isdigit(c)) return c - '0';
            if (isupper(c)) return c - 'A' + 10;
            if (islower(c)) return c - 'a' + 36;
            return 0;
        };
        int r = 0;
        for (char c : s) r = r * 60 + val(c);
        return r;
    };

    std::vector<uint8_t> cunei_key;
    std::vector<uint8_t> primes_key;
    for (size_t i = 0; i < core::secrets::CUNEIFORM_STREAM.size(); ++i) {
        int dec = b60_to_dec(core::secrets::CUNEIFORM_STREAM[i]);
        cun_csv << i << "," << core::secrets::CUNEIFORM_STREAM[i] << "," << dec << "," << (dec % 29) << "\n";
        cunei_key.push_back(static_cast<uint8_t>(dec % 29));
    }
    for (int p : core::secrets::MISSING_PRIMES_2013) {
        primes_key.push_back(static_cast<uint8_t>(p % 29));
    }
    cun_csv.close();
    std::wcout << L"  - Cuneiform stream exported (Base60 -> RuneIndex).\n";

    // 2. Exportação das Matrizes dos Secrets para Heatmap
    std::ofstream mat_csv("../output/secrets_matrices.csv");
    mat_csv << "MatrixID,Row,Col,Value\n";
    for (size_t m = 0; m < core::secrets::SQUARES.size(); ++m) {
        const auto& matrix = core::secrets::SQUARES[m];
        for (size_t r = 0; r < matrix.size(); ++r) {
            for (size_t c = 0; c < matrix[r].size(); ++c) {
                mat_csv << m << "," << r << "," << c << "," << matrix[r][c] << "\n";
            }
        }
    }
    mat_csv.close();
    std::wcout << L"  - Secret matrices exported.\n";

    // 3. Busca por Missing Primes (2013) nas somas de Gematria
    std::ofstream hits("../output/missing_primes_hits.txt");
    hits << "=== MISSING PRIMES 2013 CROSS-REFERENCE HITS ===\n\n";
    hits << "Also checking for the '3301 Spiral' constant: Sum + MissingPrime = 3301\n\n";
    std::set<int> mp_set(core::secrets::MISSING_PRIMES_2013.begin(), core::secrets::MISSING_PRIMES_2013.end());
    auto target_dist = calculate_liber_unigram_target();
    std::filesystem::create_directories("../output/decrypted");
    
    std::set<size_t> transposition_tested_pages;
    for (const auto& pg : core::G_PAGES) {
        if (pg.content.empty()) continue;
        core::ProcessedText pt(pg.content);
        auto words = pt.get_words();
        int page_hits = 0;
        int consecutive_hits = 0;

        for (size_t w_idx = 0; w_idx < words.size(); ++w_idx) {
            int g_sum = 0;
            std::string latin = "";
            for (auto r : words[w_idx]) {
                g_sum += core::RUNE_TABLE[r].prime;
                latin += core::RUNE_TABLE[r].latin;
            }

            bool is_hit = false;
            bool is_spiral = false;

            // Filtrar ruído: Apenas palavras com mais de uma runa ou somas > 120
            if (mp_set.count(g_sum) && (words[w_idx].size() > 1 || g_sum > 120)) {
                is_hit = true;
                hits << "HIT! Page: " << pg.name << " | Word Index: " << w_idx 
                     << " | Sum: " << g_sum << " | Latin: " << latin << " (Matches Missing Prime)\n";
            }

            if (mp_set.count(3301 - g_sum)) {
                is_spiral = true;
                hits << "SPIRAL HIT! Page: " << pg.name << " | Word Index: " << w_idx
                     << " | Sum: " << g_sum << " | Latin: " << latin << " | Complement: " << (3301 - g_sum) << " (is Missing Prime)\n";
            }

            if (is_hit || is_spiral) {
                page_hits++;
                consecutive_hits++;
                
                // Se detectarmos uma sequência de 3 ou mais, tentamos um ataque local
                if (consecutive_hits >= 3) {
                    hits << "  [!!!] ANCHOR SEQUENCE DETECTED at Index " << w_idx - 2 << " on " << pg.name << "\n";
                    
                    if (!core::has_known_solution(pg.index)) {
                        // TESTE DE HIPÓTESE: Cuneiform e Missing Primes como Chaves
                        for (auto& [name, key_stream] : std::vector<std::pair<std::string, std::vector<uint8_t>>>{
                            {"Cuneiform", cunei_key}, {"MissingPrimes", primes_key}
                        }) {
                            core::ProcessedText pt_test(pg.content, pg.index);
                            auto& indices = pt_test.indices();
                            size_t k_ptr = 0;
                            for(size_t i=0; i<indices.size(); ++i) {
                                if (indices[i] < 29) {
                                    int key = key_stream[k_ptr % key_stream.size()];
                                    indices[i] = static_cast<uint8_t>((static_cast<int>(indices[i]) - key + 29) % 29);
                                    k_ptr++;
                                }
                            }

                            // Testar se existe um Atbash overlay (comum no Liber Primus)
                            for (int s = 0; s < 29; ++s) {
                                core::ProcessedText pt_atbash = pt_test;
                                core::AtbashTransformer(s).transform(pt_atbash);
                                double fitness = score_text_fitness(pt_atbash, target_dist);

                                if (fitness > 0.91) {
                                    std::string full_text = pt_atbash.to_latin();
                                    hits << "  [DISCOVERY] " << name << " + Atbash+" << s << " | Fit: " << fitness << "\n";
                                    hits << "  Snippet: " << full_text.substr(0, 100) << "...\n";
                                    
                                    std::string out_fn = "../output/decrypted/" + std::string(pg.name) + "_" + name + "_Atbash" + std::to_string(s) + ".txt";
                                    std::ofstream out_f(out_fn);
                                    out_f << "Method: Vigenere (" << name << ") + Atbash(" << s << ")\n";
                                    out_f << "Fitness: " << fitness << "\n\n" << full_text;
                                    
                                    std::wcout << GREEN_COLOR << L"  [!!!] HIGH FITNESS on " << std::wstring(pg.name.begin(), pg.name.end()) 
                                               << L": " << fitness << L" (" << std::wstring(name.begin(), name.end()) << L")\n" << RESET_COLOR;
                                }
                            }
                        }

                        // TESTE DE TRANSPOSIÇÃO (Spiral, Diagonal, Columnar)
                        if (transposition_tested_pages.find(pg.index) == transposition_tested_pages.end()) {
                            transposition_tested_pages.insert(pg.index);
                            std::vector<uint8_t> clean_runes;
                            for (auto r : pt.indices()) if (r < 29) clean_runes.push_back(r);

                            if (clean_runes.size() >= 40) {
                                // Testar larguras comuns e números primos
                                for (int w : {3, 5, 7, 11, 13, 17, 19, 23, 29}) {
                                    if (static_cast<size_t>(w) >= clean_runes.size()) continue;
                                    int rows = (static_cast<int>(clean_runes.size()) + w - 1) / w;
                                    std::vector<std::vector<uint8_t>> grid(rows, std::vector<uint8_t>(w, 255));
                                    for (size_t i = 0; i < clean_runes.size(); ++i) grid[i / w][i % w] = clean_runes[i];

                                    auto check_route = [&](const std::vector<uint8_t>& reordered, const std::string& label) {
                                        core::ProcessedText pt_t(""); pt_t.indices() = reordered;
                                        for (int s = 0; s < 29; ++s) {
                                            core::ProcessedText pt_atbash = pt_t;
                                            core::AtbashTransformer(s).transform(pt_atbash);
                                            double fitness = score_text_fitness(pt_atbash, target_dist);
                                            if (fitness > 0.90) {
                                                hits << "  [DISCOVERY] Route: " << label << " (W:" << w << ") + Atbash+" << s << " | Fit: " << fitness << "\n";
                                                hits << "  Snippet: " << pt_atbash.to_latin().substr(0, 100) << "...\n";
                                                std::wcout << GREEN_COLOR << L"  [!!!] ROUTE HIT on " << std::wstring(pg.name.begin(), pg.name.end()) 
                                                           << L": " << std::wstring(label.begin(), label.end()) << RESET_COLOR << L"\n";
                                            }
                                        }
                                    };

                                    // 1. Columnar (Leitura vertical)
                                    std::vector<uint8_t> col_major;
                                    for (int c = 0; c < w; ++c) for (int r = 0; r < rows; ++r) if (grid[r][c] != 255) col_major.push_back(grid[r][c]);
                                    check_route(col_major, "Columnar");

                                    // 2. Diagonal (Zig-zag)
                                    std::vector<uint8_t> diagonal;
                                    for (int k = 0; k < rows + w - 1; ++k) {
                                        for (int j = 0; j <= k; ++j) {
                                            int r_idx = j; int c_idx = k - j;
                                            if (r_idx < rows && c_idx < w && grid[r_idx][c_idx] != 255) diagonal.push_back(grid[r_idx][c_idx]);
                                        }
                                    }
                                    check_route(diagonal, "Diagonal");

                                    // 3. Inward Spiral (Espiral para dentro)
                                    std::vector<uint8_t> spiral;
                                    int t_row = 0, b_row = rows - 1, l_col = 0, r_col = w - 1;
                                    while (t_row <= b_row && l_col <= r_col) {
                                        for (int i = l_col; i <= r_col; ++i) if(grid[t_row][i] != 255) spiral.push_back(grid[t_row][i]);
                                        t_row++;
                                        for (int i = t_row; i <= b_row; ++i) if(grid[i][r_col] != 255) spiral.push_back(grid[i][r_col]);
                                        r_col--;
                                        if (t_row <= b_row) {
                                            for (int i = r_col; i >= l_col; --i) if(grid[b_row][i] != 255) spiral.push_back(grid[b_row][i]);
                                            b_row--;
                                        }
                                        if (l_col <= r_col) {
                                            for (int i = b_row; i >= t_row; --i) if(grid[i][l_col] != 255) spiral.push_back(grid[i][l_col]);
                                            l_col++;
                                        }
                                    }
                                    check_route(spiral, "Spiral");
                                }
                            }
                        }
                    }
                }
            } else {
                consecutive_hits = 0;
            }
        }
        if (page_hits > 0) {
            std::wcout << L"  - " << std::wstring(pg.name.begin(), pg.name.end()) << L": " << page_hits << L" potential secret pointers found.\n";
        }
    }
    hits.close();
    
    std::wcout << L"[SUCCESS] Secrets analysis complete. Files generated in ../output/\n";
}

} // namespace utils