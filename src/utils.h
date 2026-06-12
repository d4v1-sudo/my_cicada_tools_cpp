#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include <array>
#include <fstream>
#include <cmath>
#include "core.h"
#include "ProcessedText.h"
#include "pages.h"
#include <ostream>
#include <string_view>

namespace utils {

    // --- Constants ---
    extern const std::vector<std::string> FIND_WORDS;
    extern const std::wstring GREEN_COLOR;
    extern const std::wstring RESET_COLOR;

    // --- Data Structures ---
    struct PageInfo {
        std::string name;
        std::array<double, 29> runic_unigram;
        std::vector<double> runic_bigram;
        std::array<double, 26> latin_unigram;
    };

    // --- Mathematical and String Utilities ---
    bool is_anagram(std::string s1, std::string s2);
    std::string_view utf8_take(std::string_view str, size_t n);
    long long gcd_vector(const std::vector<long long>& v);
    double calculate_ioc_from_indices(const std::vector<uint8_t>& indices);

    template<typename T>
    double calculate_correlation(const T& a, const T& b) {
        if (a.size() != b.size() || a.empty()) return 0.0;
        const size_t Sz = a.size();
        const double N = static_cast<double>(Sz);
        double ma = 0.0, mb = 0.0;
        for (size_t i = 0; i < Sz; ++i) { ma += a[i]; mb += b[i]; }
        ma /= N; mb /= N;
        double num = 0.0, da2 = 0.0, db2 = 0.0;
        for (size_t i = 0; i < Sz; ++i) {
            const double da = a[i] - ma;
            const double db = b[i] - mb;
            num += da * db; da2 += da * da; db2 += db * db;
        }
        const double den = std::sqrt(da2) * std::sqrt(db2);
        return (den == 0.0) ? 0.0 : num / den;
    }

    // --- Functions for Analysis and Attack ---
    void save_correlation_csv(const std::string& filename, const std::vector<PageInfo>& pages, bool is_runic);
    void run_statistical_analysis(int num_threads = 1);
    void run_oeis_search(const std::vector<core::OEISSequence>& sequences, std::ofstream& outputFile, int num_threads = 1);
    void run_heuristic_cipher_analysis(int num_threads = 1);
    void run_key_sequence_analysis(); 
    void run_skip_index_analysis();
    void run_interrupt_geometry_analysis(); 
    void run_doublet_analysis(int num_threads = 1);
    void run_advanced_signal_analysis(int num_threads = 1);
    void run_cross_page_pattern_analysis(size_t min_len = 4);
    void run_view_resolved_pages();
    void run_peak_bruteforce_analysis();
    void run_transition_correlation_analysis();
    void export_page_features(const std::string& filename = "../output/page_features.csv");
    
    void run_dictionary_vigenere_attack(
        const std::vector<std::string>& wordlist_paths,
        const std::vector<size_t>& target_page_indices,
        const std::vector<size_t>& interrupt_positions,
        double fitness_threshold = 0.92,
        const std::string& output_file = "../output/dictionary_attack.txt",
        int num_threads = 1);

    void run_rhythmic_dictionary_attack(
        const std::vector<std::string>& wordlist_paths,
        const std::vector<size_t>& target_page_indices,
        const std::vector<int>& rhythmic_deltas,
        size_t first_skip_index,
        double min_fitness,
        const std::string& output_path,
        int num_threads = 1
    );

    // functions for further analysis
    void run_delta_stream_analysis();
    void run_delta_autocorrelation_analysis();
    void run_cluster_analysis();

    void export_all_page_metrics_csv(const std::string& output_path);
    void run_friedman_key_length_scan(const std::string& output_path);

    void run_cluster_mutual_ioc_analysis(
        const std::vector<size_t>& cluster_pages,
        int suspected_len,
        const std::string& output_path
    );

    void run_rolling_ioc_analysis(size_t window_size = 30, int num_threads = 1);
    void run_kasiski_examination(size_t min_len = 3);
    void run_autokey_dictionary_attack(
        const std::vector<std::string>& wordlist_paths,
        const std::vector<size_t>& target_page_indices,
        double fitness_threshold,
        int num_threads = 1);

    // --- Helpers of UI/Console ---
    void print_hit(const std::string& type, const core::Page& page, int offset,
                   const std::string& word, const std::string& latin,
                   const std::string& mode_str);
    double calculate_chi_square(const std::array<double, 29>& observed, const std::array<double, 29>& expected, size_t n);
    std::array<double, 29> calculate_liber_unigram_target();
    std::vector<double> calculate_liber_bigram_target();
    double score_text_fitness(const core::ProcessedText& pt, const std::array<double, 29>& target_dist);
    double score_text_fitness_advanced(const core::ProcessedText& pt, const std::array<double, 29>& target_uni, const std::vector<double>& target_bi);
    void setup_console();

} // namespace utils

#endif // UTILS_H