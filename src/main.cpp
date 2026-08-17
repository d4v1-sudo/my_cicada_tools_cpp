#include <string_view>
#include <iostream>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <thread>
#include "Transformers.h"
#include "core.h"
#include "pages.h"
#include "utils.h"

int main()
{
    utils::setup_console();
    std::filesystem::create_directories("../output");
    core::init_tables();
    core::populate_primes_list(25000); // Pre-populate primes up to ~25000th prime

    double threshold = 0.85;

    int thread_count = 1;
    std::wcout << L"=== ENVIRONMENT CONFIGURATION ===\n";
    std::wcout << L"Choose processing mode:\n";
    std::wcout << L"1. Single-thread (Stable, recommended for debugging)\n";
    std::wcout << L"2. Multi-thread  (High performance, uses all cores)\n";
    
    std::string thread_input;
    std::wcout << L"Choice: ";
    std::cin >> thread_input;
    if (thread_input == "2") {
        thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) thread_count = 4;
        std::wcout << L"[INFO] Multi-thread mode enabled (" << thread_count << L" threads).\n";
    } else {
        std::wcout << L"[INFO] Single-thread mode enabled.\n";
    }

    int option = -1;
    while (option != 0) {
        std::wcout << L"\n--- CICADA 3301 C++ TOOLS ---\n"
                   << L"1. Statistical Analysis    (Correlations)\n"
                   << L"2. Rolling IoC Metrology   (Sliding Window)\n"
                   << L"3. Doublet Analysis        (Find patterns)\n"
                   << L"4. Signal Periodicity      (Delta Cycles)\n"
                   << L"5. Key Stream Analysis     (Math Properties)\n"
                   << L"6. Interrupt Geometry      (Clusters)\n"
                   << L"7. Cross-Page Patterns     (Shared N-grams)\n"
                   << L"8. View Resolved Pages     (Knowns)\n"
                   << L"9. Cipher Heuristics       (Auto-Estimator)\n"
                   << L"10. Markov Structure       (Transition Analysis)\n"
                   << L"11. Dictionary Attack      (Wordlist Vigenere)\n"
                   << L"12. OEIS Attack            (Sequence Search)\n"
                   << L"13. Rhythmic Attack        (Cyclic Deltas)\n"
                   << L"14. Peak Brute-force       (Analyze IoC Peaks)\n"
                   << L"15. Kasiski Examination    (Polyalphabetic Key Length)\n"
                   << L"16. Delta Stream Analysis  (C[i] - C[i-1])\n"
                   << L"17. Delta Autocorrelation  (Cryptographic Lags)\n"
                   << L"18. Autokey Dictionary     (Plaintext as Key)\n"
                   << L"19. Export Page Features   (CSV)\n"
                   << L"20. Friedman Key Scan      (Kl 2 to 30)\n"
                   << L"21. Cluster Mutual IoC     (Co-relation)\n"
                   << L"22. Global Cluster Analysis(Merged Blocks)\n"
                   << L"23. Auto-Vigenere Solver   (Statistical)\n"
                   << L"24. Gematria Sum Analysis   (Numerology)\n"
                   << L"25. Export Doublet Data     (CSV for Python analysis)\n"
                   << L"26. Probable Cluster Report (Multi-metric)\n"
                   << L"27. Vocabulary Overlap      (Jaccard Similarity Matrix)\n"
                   << L"28. Structural Segments     (Word/Line Length Stats)\n"
                   << L"29. Math Direction Search   (Totient/Word Sums)\n"
                   << L"30. Route Transposition     (Spiral/Diagonal/Primes)\n"
                   << L"31. Merged Corpus 17-72     (Large Block Analysis)\n"
                   << L"32. Deep Secrets Analysis    (Matrizes/Base60/Primes)\n"
                   << L"33. Manual Vigenere Attack   (Provide key)\n"
                   << L"0. Exit\n"
                   << L"Choice: ";

        if (!(std::cin >> option)) {
            std::cin.clear();
            std::cin.ignore(1000, '\n');
            continue;
        }
        std::cin.ignore();

        switch(option) {
            case 1: 
                utils::run_statistical_analysis(thread_count); 
                utils::export_page_features("../output/page_features.csv");
                break;
            case 2: utils::run_rolling_ioc_analysis(30, thread_count); break;
            case 3: utils::run_doublet_analysis(thread_count); break;
            case 4: utils::run_advanced_signal_analysis(thread_count); break;
            case 5: utils::run_key_sequence_analysis(); break;
            case 6: utils::run_interrupt_geometry_analysis(); break;
            case 7: utils::run_cross_page_pattern_analysis(); break;
            case 8: utils::run_view_resolved_pages(); break;
            case 9: 
                utils::run_heuristic_cipher_analysis(thread_count); 
                utils::export_page_features("../output/page_features.csv");
                break;
            case 10: utils::run_transition_correlation_analysis(); break;
            case 11: {
                std::vector<size_t> targets;
                for (size_t i = 16; i <= 38; ++i) targets.push_back(i);
                
                std::vector<std::string> wordlists = {
                    "../wordlists/english_wordlist.txt",
                    "../wordlists/lp_solved_wordlist.txt"
                };
                
                // Dynamically collect all unique interrupts for the target pages
                std::set<size_t> all_interrupts_set;
                for (size_t page_idx : targets) {
                    std::vector<size_t> page_interrupts = core::get_possible_interrupters(page_idx + 1); // +1 because get_possible_interrupters expects 1-based index
                    all_interrupts_set.insert(page_interrupts.begin(), page_interrupts.end());
                }
                std::vector<size_t> interrupts(all_interrupts_set.begin(), all_interrupts_set.end());
                std::sort(interrupts.begin(), interrupts.end()); // Ensure sorted if the transformer expects it
                
                utils::run_dictionary_vigenere_attack(wordlists, targets, interrupts, 0.91, "../output/dictionary_attack.txt", thread_count);
                break;
            }
            case 12: {
                auto sequences = core::SequenceTransformer::load_oeis_file("../wordlists/oeis.txt");
                if (sequences.empty()) { std::wcout << L"[ERROR] No sequences loaded from oeis.txt\n"; break; }
                std::ofstream oeis_out("../output/oeis_hits.txt", std::ios::app);
                utils::run_oeis_search(sequences, oeis_out, thread_count);
                break;
            }
            case 13: {
                std::vector<int> fib_deltas = {2, 3, 5, 8, 13, 21}; 
                std::vector<size_t> targets;
                for (size_t i = 16; i <= 38; ++i) targets.push_back(i);
                std::vector<std::string> wordlists = {"../wordlists/english_words.txt", "../wordlists/lp_solved_wordlist.txt"};
                utils::run_rhythmic_dictionary_attack(wordlists, targets, fib_deltas, 0, 0.91, "../output/rhythmic_attack.txt", thread_count);
                break;
            }
            case 14: utils::run_peak_bruteforce_analysis(); break;
            case 15: utils::run_kasiski_examination(3); break;
            case 16: utils::run_delta_stream_analysis(); break;
            case 17: utils::run_delta_autocorrelation_analysis(); break;
            case 18: {
                std::vector<size_t> targets;
                for (size_t i = 16; i <= 30; ++i) targets.push_back(i);
                std::vector<std::string> wordlists = {"../wordlists/english_words.txt", "../wordlists/lp_solved_wordlist.txt"};
                utils::run_autokey_dictionary_attack(wordlists, targets, threshold, thread_count);
                break;
            }
            case 19: utils::export_page_features("../output/page_features.csv"); break;
            case 20: {
                std::wcout << L"[RUNNING] Friedman key length scan (2 to 30)...\n";
                utils::run_friedman_key_length_scan("../output/friedman_scan.csv");
                break;
            }
            case 21: {
                std::wcout << L"Enter suspected key length to analyze the cluster: ";
                int suspected_len;
                std::cin >> suspected_len;
                std::vector<size_t> cluster_pages = {17, 18, 19, 20, 21, 22}; 
                utils::run_cluster_mutual_ioc_analysis(cluster_pages, suspected_len, "../output/cluster_mutual_ioc.csv");
                break;
            }
            case 22: utils::run_cluster_analysis(); break;
            case 23: utils::run_auto_vigenere_solver(); break;
            case 24: utils::run_gematria_sum_analysis(); break;
            case 25: {
                std::wcout << L"[RUNNING] Exporting doublet data to ../output/doublet_data.csv\n";
                utils::export_doublet_data("../output/doublet_data.csv");
                break;
            }
            case 26: utils::run_multi_metric_clustering(); break;
            case 27: utils::run_vocabulary_overlap_analysis(); break;
            case 28: utils::run_structural_segment_analysis(); break;
            case 29: utils::run_mathematical_direction_search(thread_count); break;
            case 30: {
                std::vector<size_t> targets;
                for (size_t i = 17; i <= 72; ++i) targets.push_back(i);
                utils::run_route_transposition_search(targets);
                break;
            }
            case 31: utils::run_merged_corpus_analysis(17, 72); break;
            case 32: utils::run_secrets_analysis(); break;
                case 33: {
                    std::wcout << L"Enter key (Latin or runes): ";
                    std::string key;
                    std::getline(std::cin, key);
                    if (key.empty()) { std::wcout << L"Empty key, aborting.\n"; break; }

                    std::wcout << L"Enter target pages as comma-separated (e.g. 3,4,14) or 'all': ";
                    std::string pages_line; std::getline(std::cin, pages_line);
                    std::vector<size_t> targets;
                    if (pages_line == "all") {
                        for (size_t i = 1; i <= core::G_PAGES.size(); ++i) targets.push_back(i);
                    } else {
                        std::stringstream ss(pages_line);
                        std::string tok;
                        while (std::getline(ss, tok, ',')) {
                            try { targets.push_back(std::stoul(tok)); } catch (...) {}
                        }
                    }

                    // Ask interrupt positions (optional)
                    std::wcout << L"Enter interrupt indices as comma-separated (or leave blank): ";
                    std::string ints_line; std::getline(std::cin, ints_line);
                    std::vector<size_t> interrupts;
                    if (!ints_line.empty()) {
                        std::stringstream ss2(ints_line);
                        std::string tok2;
                        while (std::getline(ss2, tok2, ',')) {
                            try { interrupts.push_back(std::stoul(tok2)); } catch (...) {}
                        }
                    }

                    utils::run_manual_vigenere_attack(key, targets, interrupts, 0.90, "../output/manual_vigenere.txt");
                    break;
                }
            default: std::wcout << L"Invalid option.\n"; break;
        }
    }
    return 0;
}