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
    // Jensen-Shannon divergence: simétrica e sempre definida mesmo quando q[i]==0
    // JSD(P||Q) = 0.5*KL(P||M) + 0.5*KL(Q||M), onde M = 0.5*(P+Q)
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
    bi_fit = (bi_fit + 1.0) / 2.0; // Normaliza de [-1,1] para [0,1]

    // 3. Entropy Penalty (Target: ~4.1 - 4.3 bits)
    double ent = pt.entropy();
    double ent_penalty = std::exp(-std::pow(ent - 4.25, 2) / 0.5); // Gaussiana centrada no ideal

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
    for (size_t i = 1; i < v.size(); ++i) r = std::gcd(r, v[i]); // <-- Alterado de core::gcd para std::gcd
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

            // Divide ambas as páginas em colunas com base no comprimento do keystream
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
            
            // Se o MIC médio cruzar 0.052, a correlação estrutural é violenta e idêntica
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
    
    // Estruturas auxiliares para capturar o índice numérico real da página
    // Isso evita o erro de ordenação alfabética (ex: Page 10 vir antes de Page 2)
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

            // Mantém todas as tuas métricas estatísticas intactas
            double unif_ioc = 1.0 / 29.0;
            double fitness = score_text_fitness(pt, liber_unigram_target);
            double chi = calculate_chi_square(pt.runic_distribution(), liber_unigram_target, pt.rune_count());
            double ioc_dev = (pt.index_of_coincidence() - unif_ioc) / unif_ioc * 100.0;

            // 1. Bufferizamos o relatório em memória local da thread
            std::ostringstream oss;
            oss << "Page: " << page.name << " (Runes: " << pt.rune_count() << ")\n"
                << "  - Runic IoC:   " << pt.index_of_coincidence() << " (" << (ioc_dev >= 0 ? "+" : "") << std::fixed << std::setprecision(1) << ioc_dev << "% vs random)\n"
                << "  - Entropy:     " << pt.entropy() << " bits\n"
                << "  - Chi-Square:  " << chi << "\n"
                << "  - Fitness:     " << std::fixed << std::setprecision(4) << fitness << "\n\n";

            // 2. Proteção mínima com mutex apenas para escrita nos vetores compartilhados
            {
                std::lock_guard<std::mutex> lock(mtx);
                std::wstring wname(page.name.begin(), page.name.end());
                std::wcout << L"[THREAD " << std::this_thread::get_id() << L"] Analyzing " << wname << L"..." << std::endl;

                // Guardamos junto com o page.index para podermos ordenar numericamente depois
                temp_pages.push_back({ page.index, {std::string(page.name), pt.runic_distribution(), pt.bigram_distribution(), pt.latin_distribution()} });
                temp_outputs.push_back({ page.index, oss.str() });
            }
        }
    };

    // Criação e execução das threads paralelas
    std::vector<std::thread> threads;
    size_t pages_per_thread = pages.size() / num_threads;
    for (int t = 0; t < num_threads; ++t) {
        size_t start = t * pages_per_thread;
        size_t end = (t == num_threads - 1) ? pages.size() : (t + 1) * pages_per_thread;
        threads.emplace_back(worker, start, end);
    }
    for (auto& t : threads) t.join();

    // 3. Ordenação estritamente NUMÉRICA baseada no índice real da página do Liber Primus
    std::sort(temp_pages.begin(), temp_pages.end(), [](const OrderedPageInfo& a, const OrderedPageInfo& b) {
        return a.original_index < b.original_index;
    });

    std::sort(temp_outputs.begin(), temp_outputs.end(), [](const OrderedTextOutput& a, const OrderedTextOutput& b) {
        return a.original_index < b.original_index;
    });

    // 4. Reconstrói o vetor plano de PageInfo esperado pelas funções de salvamento do CSV
    std::vector<PageInfo> all_pages;
    all_pages.reserve(temp_pages.size());
    for (const auto& item : temp_pages) {
        all_pages.push_back(item.info);
    }

    // 5. Escreve no arquivo de texto na ordem sequencial perfeita
    for (const auto& out : temp_outputs) {
        f << out.text;
    }

    // 6. Salvamento intocado dos CSVs que alimentam o Python Dashboard!
    save_correlation_csv("../output/corr_runic.csv", all_pages, true);
    save_correlation_csv("../output/corr_latin.csv", all_pages, false);
    
    f.close();
}

void run_friedman_key_length_scan(const std::string& output_csv_path) {
    std::ofstream csv(output_csv_path);
    if (!csv.is_open()) return;
    
    csv << "Page,KeyLen,AvgColIoC\n";
    const auto& pages = core::G_PAGES;

    for (const auto& page : pages) {
        if (page.content.empty() || core::has_known_solution(page.index)) continue;

        core::ProcessedText pt(page.content, page.index);
        if (pt.rune_count() < 40) continue;

        // Executa a varredura cíclica completa de tamanhos de chaves
        for (int kl = 2; kl <= 30; ++kl) {
            std::vector<std::array<double, 29>> col_dists(kl, std::array<double, 29>{0.0});
            std::vector<size_t> col_counts(kl, 0);
            
            size_t rune_pos = 0;
            for (auto idx : pt.indices()) {
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
                for (size_t i = 0; i < 29; ++i) {
                    sum += col_dists[c][i] * (col_dists[c][i] - 1);
                }
                avg_col_ioc += sum / (N * (N - 1.0));
            }
            avg_col_ioc /= kl;

            csv << page.name << "," << kl << "," << std::fixed << std::setprecision(5) << avg_col_ioc << "\n";
        }
    }
    csv.close();
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

            // 0. Testar Identidade
            mono_results.push_back({"None", 0, score_text_fitness(pt_original, target_dist)});

            // 1. Testar Caesar Shifts
            for (int s = 0; s < 29; ++s) {
                core::ProcessedText pt = pt_original;
                core::ShiftTransformer(s).transform(pt);
                mono_results.push_back({"Shift", s, score_text_fitness(pt, target_dist)});
            }

            // 2. Testar Atbash Variations
            for (int s = 0; s < 29; ++s) {
                core::ProcessedText pt = pt_original;
                core::AtbashTransformer(s).transform(pt);
                mono_results.push_back({"Atbash", s, score_text_fitness(pt, target_dist)});
            }

            std::sort(mono_results.begin(), mono_results.end(), [](const Result& a, const Result& b) {
                return a.fitness > b.fitness;
            });

            // 3. Vigenere brute-force por comprimento estimado (AGORA TOTALMENTE EM PARALELO!)
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
                    vigenere_oss << "    [!] KeyLen=" << kl << " ColIoC=" << std::fixed << std::setprecision(4) << avg_col_ioc << " (Friedman hit — chave provável)\n";
                    if (avg_col_ioc > max_avg_col_ioc) {
                        max_avg_col_ioc = avg_col_ioc;
                        best_vigenere_kl = kl;
                    }
                }
            }

            // Construir o log de texto localmente em memória
            std::ostringstream txt_oss;
            txt_oss << "Page: " << page.name << " (Length: " << pt_original.rune_count() << ")\n"
                    << "  Top Estimations:\n";
            size_t num_display = std::min((size_t)5, mono_results.size());
            for (size_t i = 0; i < num_display; ++i) {
                txt_oss << "    " << i+1 << ". " << std::setw(8) << std::left << mono_results[i].type << " (param: " << std::setw(2) << mono_results[i].param 
                        << ") Fitness: " << std::fixed << std::setprecision(5) << mono_results[i].fitness << "\n";
            }
            txt_oss << vigenere_oss.str() // Insere os hits de Vigenère se houverem
                    << "  Baseline IoC: " << ioc << (ioc > 0.06 ? " (High: Likely Substitution)" : " (Low: Likely Polyalphabetic)") << "\n"
                    << "------------------------------------------\n";

            // Determinar os dados finais da linha única do CSV
            std::string csv_cipher = mono_results[0].type;
            int csv_param = mono_results[0].param;
            double csv_fitness = mono_results[0].fitness;
            
            // Tratamento da coluna de melhor tamanho de chave Vigenère
            int final_vigenere_col = 0;
            if (best_vigenere_kl > 0) {
                final_vigenere_col = best_vigenere_kl;
            } else if (max_val > 0.08) {
                final_vigenere_col = auto_corr_lag;
            }

            // Se o teste de colunas do Vigenère deu um resultado muito forte, ele ganha como a cifra provável
            if (max_avg_col_ioc > 0.055) {
                csv_cipher = "Vigenere";
                csv_param = best_vigenere_kl;
                csv_fitness = max_avg_col_ioc; // Consolida o IoC de coluna como a métrica de "fitness"
            }

            // Lock ultrarrápido apenas para salvar os resultados estruturados e atualizar o console
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

    // Despachar as threads
    std::vector<std::thread> threads;
    size_t pages_per_thread = pages.size() / num_threads;
    for (int t = 0; t < num_threads; ++t) {
        size_t start = t * pages_per_thread;
        size_t end = (t == num_threads - 1) ? pages.size() : (t + 1) * pages_per_thread;
        threads.emplace_back(worker, start, end);
    }
    for (auto& t : threads) t.join();

    // Ordenar deterministicamente por nome de página alfabeticamente
    std::sort(final_results.begin(), final_results.end(), [](const CipherAnalysisResult& a, const CipherAnalysisResult& b) {
        return a.page_name < b.page_name;
    });

    // Gravar os outputs de forma limpa, linear e sem duplicatas
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

    for (size_t p = resumePage; p <= 15 && p < core::G_PAGES.size(); ++p)
    {
        std::string_view page_name = core::G_PAGES[p].name;
        std::wcout << L"\nScanning "
                   << std::wstring(page_name.begin(), page_name.end()) << L"...\n";

        std::string_view sliced = utf8_take(core::G_PAGES[p].content, 200);
        core::ProcessedText base_pt(sliced, p + 1);
        
        // Coleta posições das runas ᚠ (índice 0) para power-set de interrupções
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
            for (size_t s = start_s; s < end_s; ++s) {
                const auto& seq = sequences[s];
                
            // Checkpoint
            if (num_threads == 1) {
                std::ofstream ck("../output/stopped-at.txt", std::ios::trunc);
                ck << "PageIndex: " << p << "\nSequence: " << seq.id << "\n";
            }

            unsigned long long combos = 1ULL << f_positions.size();
            for (size_t mask = 0; mask < combos; ++mask) {
                std::vector<size_t> interrupts;
                for (size_t j = 0; j < f_positions.size(); ++j)
                    if ((mask >> j) & 1) interrupts.push_back(f_positions[j]);

                core::ProcessedText pt = base_pt;
                core::SequenceTransformer t(seq.data, interrupts);
                t.transform(pt);

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
        
        // Filtra apenas runas para o cálculo do IoC
        std::vector<uint8_t> clean_runes;
        for(auto idx : indices) if(idx < 29) clean_runes.push_back(idx);

        std::stringstream ss;
        bool resolved = core::has_known_solution(page.index);
        ss << "Page: " << page.name << (resolved ? " [STATUS: RESOLVED]" : " [STATUS: UNRESOLVED]") << "\n";

        double global_ioc = pt.index_of_coincidence();
        ss << "  Global Page IoC: " << std::fixed << std::setprecision(5) << global_ioc << "\n";
        
        // Referência: 0.034 = Aleatório | 0.067 = Texto Plano Liber Primus
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

        // Criamos uma cópia para ordenar os resultados apenas para o log de texto (.txt)
        // Isso permite ver os pontos de maior interesse primeiro sem bagunçar o CSV.
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
            // Salva TODOS os dados de IoC para o CSV
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

        // Encontra a página nos dados globais
        for (const auto& pg : core::G_PAGES) {
            if (pg.name == page_name) {
                core::ProcessedText pt_full(pg.content);
                auto& full_indices = pt_full.indices();
                
                // Extrai apenas as runas da janela
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

                // Se o IoC global da página for muito baixo, avisa que o brute-force simples pode falhar
                bool resolved = core::has_known_solution(pg.index);
                if (!resolved && pt_full.index_of_coincidence() < 0.04) {
                    static std::set<std::string> warned_pages;
                    if (warned_pages.find(pg.name.data()) == warned_pages.end()) {
                        std::wcout << L"[INFO] " << std::wstring(pg.name.begin(), pg.name.end()) << L" has low IoC. Consider rhythmic attacks.\n";
                        warned_pages.insert(std::string(pg.name.data()));
                    }
                }

                if(window_indices.empty()) continue;

                // Brute force local na janela
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

        if (solved) {
            for (size_t j = 0; j < c_idx.size(); ++j) {
                if (c_idx[j] < 29 && p_idx[j] < 29)
                    stream.push_back((static_cast<int>(c_idx[j]) - static_cast<int>(p_idx[j]) + 29) % 29);
            }
            f << "Page: " << pg.name << " | Values: ";
            for (size_t j = 0; j < std::min(stream.size(), (size_t)20); ++j) f << stream[j] << " ";
            f << (stream.size() > 20 ? "...\n" : "\n");

            // Exporta stream completa no CSV
            for (size_t j = 0; j < stream.size(); ++j) {
                csv << pg.name << "," << j << "," << stream[j] << "\n";
            }
        } else {
            // Para páginas não resolvidas, ainda registrar a presença e marca-las como UNRESOLVED
            f << "Page: " << pg.name << " | Values: [UNRESOLVED]\n";
            csv << pg.name << ",-1,UNRESOLVED\n";
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

    // 1. Coleta de dados e exibição da sequência de contadores
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

    // 2. Identificação de Clusters (Páginas com assinaturas idênticas)
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

    // 3. Investigação de Anomalias (Page 16 e vizinhos)
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

    // 4. Exportação da sequência pura de contadores para análise externa
    f << "\n--- RAW COUNT SEQUENCE (Possible Meta-Message) ---\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        f << entries[i].indices.size() << (i == entries.size() - 1 ? "" : ", ");
    }
    f << "\n";

    f.close();
    
    std::wcout << L"Analysis of " << entries.size() << L" pages concluded.\n";
    std::wcout << GREEN_COLOR << L"Clusters and raw sequences saved to ../output/interrupt_clusters.txt" << RESET_COLOR << L"\n";
    
    // Print especial para a sequência no console
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
        if (ratio > 0.02) { // Picos de repetição > 2% sugerem algo não aleatório
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

        // Busca por periodicidade nos deltas (Testando hipótese de PRNG cíclico)
        std::stringstream ss;
        for (size_t period = 2; period <= deltas.size() / 2; ++period) {
            int matches = 0;
            for (size_t i = 0; i < deltas.size() - period; ++i) {
                if (deltas[i] == deltas[i + period]) matches++;
            }
            double score = static_cast<double>(matches) / static_cast<double>(deltas.size() - period);
            
            if (score > 0.4) { // Alta repetição de padrão de salto
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
    std::map<std::vector<uint8_t>, std::vector<std::string>> pattern_map;

    for (const auto& page : core::G_PAGES) {
        if (page.content.empty()) continue;
        core::ProcessedText pt(page.content);
        auto& clean = pt.indices(); // Simplificando para usar indices brutos
        std::vector<uint8_t> runes;
        for(auto x : clean) if(x < 29) runes.push_back(x);

        if(runes.size() < min_len) continue;
        for (size_t i = 0; i <= runes.size() - min_len; ++i) {
            std::vector<uint8_t> seq(runes.begin() + i, runes.begin() + i + min_len);
            pattern_map[seq].push_back(std::string(page.name));
        }
    }

    for (auto const& [seq, pages] : pattern_map) {
        std::set<std::string> unique_pages(pages.begin(), pages.end());
        if (unique_pages.size() > 1) {
            f << "Pattern [ ";
            for(auto x : seq) f << (int)x << " ";
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

        std::map<std::vector<uint8_t>, std::vector<size_t>> sequences;
        for (size_t i = 0; i <= runes.size() - min_len; ++i) {
            std::vector<uint8_t> seq(runes.begin() + i, runes.begin() + i + min_len);
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
                for (auto x : seq) f << (int)x << " ";
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
    
    f << "Page,Length,IoC,Entropy,ChiSquare,Fitness,BigramIoC,NumInterrupts,GpsSumMean,TopDelta,LRS\n";
    
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
        
        // Longest Repeated Substring (LRS) as a basic indicator of periodicity
        auto auto_corr = pt.autocorrelation();
        double max_auto = (auto_corr.size() > 2) ? *std::max_element(auto_corr.begin()+2, auto_corr.end()) : 0;

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
          << max_auto << "\n";
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
        
        // Cálculo de Entropia dos Deltas
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
        for (size_t i = 1; i < runes.size(); ++i) {
            deltas.push_back(static_cast<uint8_t>(
                (static_cast<int>(runes[i]) - static_cast<int>(runes[i - 1]) + 29) % 29
            ));
        }

        // Analisar lags de 1 até 200 (ou até metade da página)
        int max_lag = std::min<int>(200, deltas.size() / 2);
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
    std::wcout << L"\n=== GLOBAL CLUSTER ANALYSIS (MERGED BLOCKS) ===\n";
    std::ofstream txt("../output/cluster_analysis.txt");

    // Lembrete: Índices no C++ são (Número_da_Página - 1)
    struct ClusterDef {
        std::string name;
        std::vector<size_t> page_indices;
    };

    std::vector<ClusterDef> clusters = {
        {"Cluster_A_Pages_17_to_19", {16, 17, 18}},
        {"Cluster_B_Pages_20_to_24", {19, 20, 21, 22, 23}},
        {"Cluster_C_Pages_25_to_31", {24, 25, 26, 27, 28, 29, 30}},
        {"Cluster_D_Pages_33_to_39", {32, 33, 34, 35, 36, 37, 38}}
    };

    for (const auto& cluster : clusters) {
        std::vector<uint8_t> merged_runes;

        // 1. Concatenar todas as runas das páginas do cluster
        for (size_t idx : cluster.page_indices) {
            if (idx < core::G_PAGES.size() && !core::G_PAGES[idx].content.empty()) {
                core::ProcessedText pt(core::G_PAGES[idx].content);
                for (auto r : pt.indices()) {
                    if (r < 29) merged_runes.push_back(r);
                }
            }
        }

        if (merged_runes.empty()) continue;

        double ioc = calculate_ioc_from_indices(merged_runes);

        txt << "--- " << cluster.name << " ---\n"
            << "Total Runes: " << merged_runes.size() << "\n"
            << "Global IoC:  " << std::fixed << std::setprecision(5) << ioc << "\n\n";

        // 2. Kasiski Global no Bloco (tamanho mínimo 4)
        std::map<std::vector<uint8_t>, std::vector<size_t>> sequences;
        size_t min_len = 4;
        for (size_t i = 0; i + min_len <= merged_runes.size(); ++i) {
            std::vector<uint8_t> seq(merged_runes.begin() + i, merged_runes.begin() + i + min_len);
            sequences[seq].push_back(i);
        }

        txt << "Top Kasiski Spacings (Len >= " << min_len << "):\n";
        bool kasiski_found = false;
        for (auto const& [seq, pos] : sequences) {
            if (pos.size() > 2) { // Mais de 2 aparições espalhadas pelo cluster
                kasiski_found = true;
                txt << "  Pattern [ ";
                for (auto x : seq) txt << (int)x << " ";
                txt << "] Positions: ";
                std::vector<long long> deltas;
                for (size_t i = 0; i < pos.size(); ++i) {
                    txt << pos[i] << (i == pos.size() - 1 ? "" : ", ");
                    if (i > 0) deltas.push_back(pos[i] - pos[i - 1]);
                }
                txt << " | GCD: " << gcd_vector(deltas) << "\n";
            }
        }
    if (!kasiski_found) txt << "  (No strong repeated long pattern found)\n";

        // 3. Friedman Global Varredura (Lag 2 a 120)
    txt << "\nFriedman scan in block (Suspected lags > 0.045):\n";
        for (int kl = 2; kl <= 120; ++kl) {
            if (kl > merged_runes.size() / 2) break;

            std::vector<size_t> col_counts(kl, 0);
            std::vector<std::array<double, 29>> col_dists(kl, std::array<double, 29>{0.0});

            for (size_t i = 0; i < merged_runes.size(); ++i) {
                col_dists[i % kl][merged_runes[i]]++;
                col_counts[i % kl]++;
            }

            double avg_col_ioc = 0.0;
            for (int c = 0; c < kl; ++c) {
                if (col_counts[c] <= 1) continue;
                double sum = 0.0;
                double N = static_cast<double>(col_counts[c]);
                for (size_t i = 0; i < 29; ++i) {
                    sum += col_dists[c][i] * (col_dists[c][i] - 1.0);
                }
                avg_col_ioc += sum / (N * (N - 1.0));
            }
            avg_col_ioc /= kl;

            // Limite sensível para relatar potenciais comprimentos de chave periódica longos
            if (avg_col_ioc > 0.045) { 
                txt << "  KeyLen " << std::setw(3) << kl << ": Avg Col IoC = " << std::fixed << std::setprecision(5) << avg_col_ioc << "\n";
            }
        }
        txt << "\n==========================================\n\n";
    }

    txt.close();
    std::wcout << L"Cluster analysis saved to ../output/cluster_analysis.txt\n";
}

} // namespace utils