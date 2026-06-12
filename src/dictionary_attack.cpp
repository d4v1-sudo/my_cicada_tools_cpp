// dictionary_attack.cpp
#include "utils.h"
#include "Transformers.h"
#include "pages.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <atomic>
#include <set> // Adicionado para validação de bigramas

namespace utils {

// Lista explícita de bigramas aceitáveis no inglês/runas do Liber Primus para evitar falsos positivos
static const std::set<std::string> VALID_BIGRAMS = {"TH", "NG", "AE", "EA", "EO", "OE", "IA"};

// Carrega wordlist de arquivo (uma palavra por linha, uppercase)
static std::vector<std::string> load_wordlist(const std::string& path) {
    std::vector<std::string> words;
    std::ifstream f(path);
    if (!f.is_open()) return words;
    
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
    for (auto& c : line) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        
        bool valid = true;
        for (char c : line) {
            if (!isalpha(c)) { valid = false; break; }
        }
        if (valid && line.size() >= 3 && line.size() <= 20) {
            words.push_back(line);
        }
    }
    return words;
}

// Converte palavra inglesa para índices rúnicos tratando adequadamente bigramas linguísticos
static std::vector<uint8_t> word_to_rune_indices(const std::string& word) {
    std::vector<uint8_t> result;
    size_t i = 0;
    while (i < word.size()) {
        bool found = false;
        
        // CORREÇÃO 4: Só consome o bigrama se ele for linguisticamente válido
        if (i + 1 < word.size()) {
            std::string bigram = word.substr(i, 2);
            if (VALID_BIGRAMS.count(bigram)) {
                auto idx = core::RuneDB::instance().get_index_latin(bigram);
                if (idx) {
                    result.push_back(static_cast<uint8_t>(*idx));
                    i += 2; 
                    found = true;
                }
            }
        }
        if (!found) {
            std::string ch = word.substr(i, 1);
            auto idx = core::RuneDB::instance().get_index_latin(ch);
            if (idx) {
                result.push_back(static_cast<uint8_t>(*idx));
            } else {
                return {}; // Letra inválida/não mapeada (ex: Q, Z) -> descarta palavra
            }
            i++;
        }
    }
    return result;
}

struct PreprocessedWord {
    std::string original;
    std::vector<uint8_t> indices;
};

void run_dictionary_vigenere_attack(
    const std::vector<std::string>& wordlist_paths,
    const std::vector<size_t>& target_page_indices,  
    const std::vector<size_t>& interrupt_positions,  
    double fitness_threshold,                         
    const std::string& output_file,
    int num_threads)
{
    std::vector<std::string> all_words;
    for (const auto& path : wordlist_paths) {
        auto words = load_wordlist(path);
        all_words.insert(all_words.end(), words.begin(), words.end());
    }

    if (all_words.empty()) {
        std::wcout << L"[ERROR] No words loaded from wordlists.\n";
        return;
    }

    std::sort(all_words.begin(), all_words.end());
    all_words.erase(std::unique(all_words.begin(), all_words.end()), all_words.end());

    std::vector<PreprocessedWord> prepared_words;
    prepared_words.reserve(all_words.size());
    for (const auto& w : all_words) {
        auto idxs = word_to_rune_indices(w);
        if (!idxs.empty()) {
            prepared_words.push_back({w, std::move(idxs)});
        }
    }

    if (prepared_words.empty()) return;

    std::vector<size_t> sorted_interrupts = interrupt_positions;
    std::sort(sorted_interrupts.begin(), sorted_interrupts.end());

    auto target_dist = calculate_liber_unigram_target();
    std::ofstream out(output_file);
    std::mutex out_mtx;
    out << "=== DICTIONARY VIGENERE ATTACK ===\n";
    out << "Wordlists: " << all_words.size() << " unique words total\n";
    out << "Threshold: " << fitness_threshold << "\n\n";

    std::wcout << L"Starting multithreaded attack with " << prepared_words.size()
               << L" words against " << target_page_indices.size() << L" pages...\n";

    std::atomic<size_t> total_tested{0};
    std::atomic<size_t> total_hits{0};

    for (size_t page_idx : target_page_indices) {
        if (page_idx >= core::G_PAGES.size()) continue;
        const auto& page = core::G_PAGES[page_idx];
        if (page.content.empty()) continue;

        core::ProcessedText pt_original(page.content, page.index);
        if (pt_original.rune_count() < 30) continue;

        std::wstring wname(page.name.begin(), page.name.end());
        std::wcout << L"  Attacking " << wname << L"...\n";

        struct Hit { std::string word; double fitness; std::string preview; };
        std::vector<Hit> page_hits;
        std::mutex page_hits_mtx;

        auto worker = [&](size_t start, size_t end) {
            std::vector<uint8_t> test_buffer;
            test_buffer.reserve(pt_original.indices().size());

            for (size_t i = start; i < end; ++i) {
                const auto& prep_word = prepared_words[i];
                const auto& key_indices = prep_word.indices;

                test_buffer = pt_original.indices();
                
                size_t r_pos = 0; size_t k_ptr = 0; size_t i_ptr = 0;
                    for (auto& idx : test_buffer) {
                    if (idx >= 29) continue;

                    if (i_ptr < sorted_interrupts.size() && sorted_interrupts[i_ptr] == r_pos) {
                        i_ptr++;
                    } else {
                        {
                            int tmp = static_cast<int>(idx);
                            int key = static_cast<int>(key_indices[k_ptr % key_indices.size()]);
                            tmp = (tmp - key + 29) % 29;
                            idx = static_cast<uint8_t>(tmp);
                        }
                        k_ptr++;
                    }
                    r_pos++;
                }

                core::ProcessedText pt("", page.index);
                pt.indices() = test_buffer; 

                total_tested++;
                double fitness = score_text_fitness(pt, target_dist);

                if (fitness >= fitness_threshold) {
                    std::string latin = pt.to_latin();
                    bool has_keyword = false;
                    for (const auto& kw : FIND_WORDS) {
                        if (latin.find(kw) != std::string::npos) { has_keyword = true; break; }
                    }

                    {
                        std::lock_guard<std::mutex> lock(page_hits_mtx);
                        page_hits.push_back({
                            prep_word.original, fitness,
                            std::string(utf8_take(latin, 120))
                        });
                    }
                    total_hits++;

                    if (has_keyword) {
                        std::lock_guard<std::mutex> lock(out_mtx);
                        std::wcout << GREEN_COLOR << L"  [!!! KEYWORD HIT] "
                                   << std::wstring(prep_word.original.begin(), prep_word.original.end())
                                   << L" on " << wname << RESET_COLOR << L"\n";
                        out << "*** KEYWORD HIT ***\n";
                        out << "Page: " << page.name << "\n";
                        out << "Key:  " << prep_word.original << "\n";
                        out << "Fitness: " << std::fixed << std::setprecision(5) << fitness << "\n";
                        out << "Preview: " << latin << "\n---\n";
                    }
                }
            }
        };

        std::vector<std::thread> threads;
        size_t words_per_thread = prepared_words.size() / num_threads;
        for (int t = 0; num_threads > 1 && t < num_threads - 1; ++t) {
            threads.emplace_back(worker, t * words_per_thread, (t + 1) * words_per_thread);
        }
        worker((num_threads - 1) * words_per_thread, prepared_words.size());

        for (auto& t : threads) t.join();

        if (!page_hits.empty()) {
            std::sort(page_hits.begin(), page_hits.end(),
                      [](const Hit& a, const Hit& b){ return a.fitness > b.fitness; });
            {
                std::lock_guard<std::mutex> lock(out_mtx);
                out << "Page: " << page.name << " — Top " 
                    << std::min(page_hits.size(), (size_t)10) << " hits:\n";
                for (size_t i = 0; i < std::min(page_hits.size(), (size_t)10); ++i) {
                    out << "  [" << i+1 << "] Key=" << std::setw(20) << std::left
                        << page_hits[i].word
                        << " Fitness=" << std::fixed << std::setprecision(5)
                        << page_hits[i].fitness << "\n"
                        << "      " << page_hits[i].preview << "\n";
                }
                out << "\n";
            }
        }
    }

    out << "\n=== SUMMARY ===\n";
    out << "Total tested: " << total_tested << "\n";
    out << "Total hits (>= threshold): " << total_hits << "\n";
    std::wcout << L"\nDictionary attack complete. "
               << total_hits << L" hits. See " 
               << std::wstring(output_file.begin(), output_file.end()) << L"\n";
}

void run_rhythmic_dictionary_attack(
    const std::vector<std::string>& wordlist_paths,
    const std::vector<size_t>& target_page_indices,
    const std::vector<int>& rhythm_deltas,
    size_t first_skip_index,
    double fitness_threshold,
    const std::string& output_file,
    int num_threads)
{
    std::vector<std::string> all_words;
    for (const auto& path : wordlist_paths) {
        auto words = load_wordlist(path);
        all_words.insert(all_words.end(), words.begin(), words.end());
    }

    if (all_words.empty()) {
        std::wcout << L"[ERROR] Nenhuma palavra carregada das wordlists rítmicas. Verifique os caminhos.\n";
        return;
    }

    std::sort(all_words.begin(), all_words.end());
    all_words.erase(std::unique(all_words.begin(), all_words.end()), all_words.end());

    std::vector<PreprocessedWord> prepared_words;
    for (const auto& w : all_words) {
        auto idxs = word_to_rune_indices(w);
        if (!idxs.empty()) prepared_words.push_back({w, std::move(idxs)});
    }

    if (prepared_words.empty()) {
        std::wcout << L"[ERROR] Nenhuma das " << all_words.size() << L" palavras eh valida para o alfabeto runico.\n";
        return;
    }

    auto target_dist = calculate_liber_unigram_target();
    std::ofstream out(output_file);
    std::mutex out_mtx;
    
    std::atomic<size_t> total_tested{0};
    std::atomic<size_t> total_hits{0};

    out << "=== RHYTHMIC VIGENERE ATTACK ===\n";
    out << "Deltas: "; for(int d : rhythm_deltas) out << d << " ";
    out << "\nThreshold: " << fitness_threshold << "\n\n";

    std::wcout << L"Starting Rhythmic Attack (" << rhythm_deltas.size() << L" deltas) against " 
               << target_page_indices.size() << L" pages...\n";

    for (size_t page_idx : target_page_indices) {
        if (page_idx >= core::G_PAGES.size()) continue;
        const auto& page = core::G_PAGES[page_idx];
        if (page.content.empty()) continue;

        core::ProcessedText pt_original(page.content, page.index);
        if (pt_original.rune_count() < 20) continue;

        std::wstring wname(page.name.begin(), page.name.end());
        std::wcout << L"  Attacking " << wname << L" (" << pt_original.rune_count() << L" runes)...\n";

        struct Hit { std::string word; double fitness; std::string preview; };
        std::vector<Hit> page_hits;
        std::mutex page_hits_mtx;
        
        auto worker = [&](size_t start, size_t end) {
            std::vector<uint8_t> test_buffer;
            test_buffer.reserve(pt_original.indices().size());

            for (size_t i = start; i < end; ++i) {
                const auto& prep_word = prepared_words[i];
                const auto& key_indices = prep_word.indices;

                test_buffer = pt_original.indices();
                
                size_t r_pos = 0; 
                size_t k_ptr = 0; 
                size_t d_ptr = 0;
                size_t next_skip = first_skip_index;

                for(auto& idx : test_buffer) {
                    if (idx >= 29) continue;

                    if (r_pos == next_skip) {
                        if (!rhythm_deltas.empty()) {
                            next_skip += rhythm_deltas[d_ptr % rhythm_deltas.size()];
                            d_ptr++;
                        } else {
                            next_skip = 999999;
                        }
                    } else {
                        {
                            int tmp = static_cast<int>(idx);
                            int key = static_cast<int>(key_indices[k_ptr % key_indices.size()]);
                            tmp = (tmp - key + 29) % 29;
                            idx = static_cast<uint8_t>(tmp);
                        }
                        k_ptr++;
                    }
                    r_pos++;
                }

                core::ProcessedText pt("", page.index);
                pt.indices() = test_buffer;
                total_tested++;
                double fitness = score_text_fitness(pt, target_dist);

                if (fitness >= fitness_threshold) {
                    std::string latin = pt.to_latin();
                    
                    {
                        std::lock_guard<std::mutex> lock(page_hits_mtx);
                        page_hits.push_back({
                            prep_word.original, fitness,
                            std::string(utf8_take(latin, 120))
                        });
                    }

                    bool has_keyword = false;
                    for (const auto& kw : FIND_WORDS) {
                        if (latin.find(kw) != std::string::npos) { has_keyword = true; break; }
                    }

                    if (has_keyword) {
                        std::lock_guard<std::mutex> lock(out_mtx);
                        total_hits++;
                        std::wcout << GREEN_COLOR << L"  [!!! RHYTHMIC HIT] "
                                   << std::wstring(prep_word.original.begin(), prep_word.original.end())
                                   << L" on " << wname << RESET_COLOR << L"\n";
                        out << "*** RHYTHMIC KEYWORD HIT ***\n"
                            << "Page: " << page.name << "\n"
                            << "Key:  " << prep_word.original << "\n"
                            << "Fitness: " << std::fixed << std::setprecision(5) << fitness << "\n"
                            << "Preview: " << latin << "\n---\n";
                    }
                }
            }
        };

        std::vector<std::thread> threads;
        size_t words_per_thread = prepared_words.size() / num_threads;
        for (int t = 0; num_threads > 1 && t < num_threads - 1; ++t) {
            threads.emplace_back(worker, t * words_per_thread, (t + 1) * words_per_thread);
        }
        worker((num_threads - 1) * words_per_thread, prepared_words.size());
        for (auto& t : threads) t.join();

        if (!page_hits.empty()) {
            std::sort(page_hits.begin(), page_hits.end(), [](const Hit& a, const Hit& b){ return a.fitness > b.fitness; });
            std::lock_guard<std::mutex> lock(out_mtx);
            out << "Page: " << page.name << " — Top Candidate Keys:\n";
            for (size_t i = 0; i < std::min(page_hits.size(), (size_t)10); ++i) {
                out << "  [" << i+1 << "] Key=" << std::setw(15) << std::left << page_hits[i].word 
                    << " Fitness=" << std::fixed << std::setprecision(5) << page_hits[i].fitness << "\n"
                    << "      " << page_hits[i].preview << "\n";
            }
            out << "\n";
        }
    }
    
    std::wcout << L"Rhythmic attack complete. Tested: " << total_tested 
               << L", Keyword Hits: " << total_hits << L"\n";
}

void run_autokey_dictionary_attack(
    const std::vector<std::string>& wordlist_paths,
    const std::vector<size_t>& target_page_indices,
    double fitness_threshold, 
    int num_threads)
{
    std::vector<std::string> all_words;
    for (const auto& path : wordlist_paths) {
        auto words = load_wordlist(path);
        all_words.insert(all_words.end(), words.begin(), words.end());
    }

    if (all_words.empty()) {
        std::wcout << L"[ERROR] Nenhuma palavra carregada para o ataque Autokey.\n";
        return;
    }

    std::sort(all_words.begin(), all_words.end());
    all_words.erase(std::unique(all_words.begin(), all_words.end()), all_words.end());

    std::vector<PreprocessedWord> prepared_seeds;
    for (const auto& w : all_words) {
        auto idxs = word_to_rune_indices(w);
        if (!idxs.empty()) prepared_seeds.push_back({w, std::move(idxs)});
    }

    auto target_dist = calculate_liber_unigram_target();
    std::ofstream out("../output/autokey_attack.txt");
    std::mutex out_mtx;
    
    std::atomic<size_t> total_tested{0};
    out << "=== DICTIONARY AUTOKEY ATTACK ===\n";
    out << "Threshold: " << fitness_threshold << "\n\n";

    std::wcout << L"Starting Autokey Attack with " << prepared_seeds.size() 
               << L" seeds against " << target_page_indices.size() << L" pages...\n";

    for (size_t page_idx : target_page_indices) {
        if (page_idx >= core::G_PAGES.size()) continue;
        const auto& page = core::G_PAGES[page_idx];
        if (page.content.empty()) continue;

        core::ProcessedText pt_original(page.content, page.index);
        if (pt_original.rune_count() < 30) continue;

        auto interrupts = core::get_possible_interrupters(page.index);
        std::set<size_t> interrupt_set(interrupts.begin(), interrupts.end());

        std::wstring wname(page.name.begin(), page.name.end());
        std::wcout << L"  Attacking " << wname << L"...\n";

        struct Hit { std::string word; double fitness; std::string preview; bool is_ciphertext; };
        std::vector<Hit> page_hits;
        std::mutex page_hits_mtx;

        auto worker = [&](size_t start, size_t end) {
            std::vector<uint8_t> test_buffer;
            std::vector<uint8_t> history; 
            test_buffer.reserve(pt_original.indices().size());

            for (size_t i = start; i < end; ++i) {
                const auto& prep_word = prepared_seeds[i];
                const auto& seed_indices = prep_word.indices;
                size_t seed_len = seed_indices.size();

                for (bool ciphertext_mode : {false, true}) {
                    test_buffer = pt_original.indices();
                    history.clear();
                    size_t rune_pos = 0;

                    for(auto& idx : test_buffer) {
                        if (idx >= 29) continue;
                        if (interrupt_set.count(rune_pos)) { rune_pos++; continue; }

                        int cur_c = static_cast<int>(idx);
                        int key = (history.size() < seed_len) ? static_cast<int>(seed_indices[history.size()]) : static_cast<int>(history[history.size()-seed_len]);
                        int tmp_p = (cur_c - key + 29) % 29;
                        uint8_t p = static_cast<uint8_t>(tmp_p);

                        if (ciphertext_mode) history.push_back(idx); else history.push_back(p);
                        idx = p;
                        rune_pos++;
                    }

                    core::ProcessedText pt("", page.index);
                    pt.indices() = test_buffer;
                    total_tested++;
                    double fitness = score_text_fitness(pt, target_dist);

                    // CORREÇÃO 3: Substituído o 0.90 fixo pela variável parametrizada
                    if (fitness >= fitness_threshold) {
                        std::string latin = pt.to_latin();
                        {
                            std::lock_guard<std::mutex> lock(page_hits_mtx);
                            page_hits.push_back({prep_word.original, fitness, std::string(utf8_take(latin, 120)), ciphertext_mode});
                        }
                        for (const auto& kw : FIND_WORDS) {
                            if (latin.find(kw) != std::string::npos) {
                                std::lock_guard<std::mutex> lock(out_mtx);
                                std::wcout << GREEN_COLOR << L"  [!!! " << (ciphertext_mode ? L"CTEXT" : L"PLAIN") << L" HIT] " << std::wstring(prep_word.original.begin(), prep_word.original.end()) << RESET_COLOR << L"\n";
                                out << "*** HIT (" << (ciphertext_mode ? "CIPHER" : "PLAIN") << ") ***\nPage: " << page.name << "\nSeed: " << prep_word.original << "\nText: " << latin << "\n---\n";
                                break;
                            }
                        }
                    }
                }
            }
        };

        std::vector<std::thread> threads;
        size_t chunk = prepared_seeds.size() / num_threads;
        for (int t = 0; t < num_threads - 1; ++t) threads.emplace_back(worker, t * chunk, (t + 1) * chunk);
        worker((num_threads - 1) * chunk, prepared_seeds.size());
        for (auto& t : threads) t.join();

        if (!page_hits.empty()) {
            std::sort(page_hits.begin(), page_hits.end(), [](const Hit& a, const Hit& b){ return a.fitness > b.fitness; });
            std::lock_guard<std::mutex> lock(out_mtx);
            out << "Page: " << page.name << " - Top Candidates:\n";
            for (size_t i = 0; i < std::min(page_hits.size(), (size_t)5); ++i) {
                out << "  Seed=" << std::setw(15) << page_hits[i].word << " Fit=" << page_hits[i].fitness << " Mode=" << (page_hits[i].is_ciphertext ? "Cipher" : "Plain") << "\n    " << page_hits[i].preview << "\n";
            }
        }
    }
    std::wcout << L"Autokey attack complete. Tested " << total_tested << L" seeds.\n";
}

} // namespace utils