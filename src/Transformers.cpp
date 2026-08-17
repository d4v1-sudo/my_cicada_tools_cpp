#include "Transformers.h"
#include "core.h"
#include <fstream>
#include <string>
#include <algorithm>
#include <sstream>

namespace core
{
    VigenereTransformer::VigenereTransformer(const std::string& utf8_key, 
                                           const std::vector<size_t>& interrupt_indices)
    {
        auto maybe_indices = to_rune_indices(utf8_key);
        if (maybe_indices) m_key_indices = *maybe_indices;
        for (auto idx : interrupt_indices) m_interrupts.insert(idx);
    }

    void VigenereTransformer::transform(ProcessedText& pt)
    {
        if (m_key_indices.empty()) return;
        auto& indices = pt.indices();
        size_t rune_pos = 0;
        size_t key_ptr = 0;

        for (size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] >= 29) continue;

            if (m_interrupts.count(rune_pos)) {
                rune_pos++;
                continue;
            }

            int val = static_cast<int>(indices[i]);
            int key = static_cast<int>(m_key_indices[key_ptr % m_key_indices.size()]);
            
            // Decryption: (Index - Key) mod 29
            {
                int tmp = (static_cast<int>(val) - static_cast<int>(key) + 29) % 29;
                indices[i] = static_cast<uint8_t>(tmp);
            }
            
            key_ptr++;
            rune_pos++;
        }
    }

    SequenceTransformer::SequenceTransformer(const std::vector<int>& sequence, 
                                            const std::vector<size_t>& interrupt_indices)
        : m_sequence(sequence)
    {
        for (auto idx : interrupt_indices) m_interrupts.insert(idx);
    }

    void SequenceTransformer::transform(ProcessedText& pt)
    {
        if (m_sequence.empty()) return;

        auto& indices = pt.indices();
        size_t actual_rune_idx_in_sequence = 0; // This counter advances only for runes

        for (size_t i = 0; i < indices.size(); ++i)
        {
            if (indices[i] >= 29) { // Delimiter or special char — skip transformation
                continue; 
            }

            // It's a rune. Check for interrupts based on the rune sequence position.
            if (m_interrupts.count(actual_rune_idx_in_sequence)) {
                actual_rune_idx_in_sequence++; // Still a rune but interrupted, advance the rune position
                continue;
            }

            // Decryption: (Index - SequenceValue) % 29
            int val = static_cast<int>(indices[i]);
            int key = m_sequence[actual_rune_idx_in_sequence % m_sequence.size()]; // Uses actual_rune_idx_in_sequence
            
            {
                int tmp = (static_cast<int>(val) - static_cast<int>(key % 29) + 29) % 29;
                indices[i] = static_cast<uint8_t>(tmp);
            }
            actual_rune_idx_in_sequence++;
        }
    }

    std::vector<OEISSequence> SequenceTransformer::load_oeis_file(const std::string& filename)
    {
        std::vector<OEISSequence> sequences;
        std::ifstream file(filename);
        if (!file.is_open()) return sequences;

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            OEISSequence seq_entry;
            std::stringstream ss(line);
            std::string item;
            bool is_first = true;

            while (std::getline(ss, item, ',')) {
                if (is_first) { 
                    seq_entry.id = item;
                    is_first = false;
                    continue; 
                }
                if (!item.empty()) {
                    char* end;
                    long val = std::strtol(item.c_str(), &end, 10);
                    if (end != item.c_str()) {
                        seq_entry.data.push_back(static_cast<int>(val));
                    }
                }
            }

            if (seq_entry.data.size() >= 10) {
                sequences.push_back(seq_entry);
            }
        }
        return sequences;
    }

    PrimeStreamTransformer::PrimeStreamTransformer(PrimeOperation op, PrimeKeyDerivationMode mode, 
                                                   size_t initial_prime_idx, 
                                                   const std::set<size_t>& custom_interrupts,
                                                   int tot_calls, bool emirp)
        : m_operation(op), m_mode(mode), m_initial_prime_idx(initial_prime_idx), 
          m_custom_interrupts(custom_interrupts), m_tot_calls(tot_calls), m_emirp(emirp) {}

    void PrimeStreamTransformer::transform(ProcessedText& pt) {
        auto& indices = pt.indices();
        size_t rune_pos = 0;
        size_t prime_ptr = m_initial_prime_idx;

        for (size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] >= 29) continue;

            if (m_custom_interrupts.count(rune_pos)) {
                rune_pos++;
                prime_ptr++; // Advance prime even on interrupt to match known solutions
                continue;
            }

            int p = (prime_ptr < G_PRIMES.size()) ? G_PRIMES[prime_ptr] : find_next_prime(G_PRIMES.back());
            
            if (m_emirp) p = reverse_digits(p);

            int key_val = 0;
            switch (m_mode) {
                case PrimeKeyDerivationMode::PRIME: key_val = p; break;
                case PrimeKeyDerivationMode::PRIME_MINUS_ONE: key_val = p - 1; break;
                case PrimeKeyDerivationMode::PRIME_PLUS_ONE: key_val = p + 1; break;
                case PrimeKeyDerivationMode::TOTIENT_PRIME: key_val = euler_totient(p); break;
                case PrimeKeyDerivationMode::TOTIENT_PRIME_MINUS_ONE: key_val = euler_totient(p - 1); break;
            }

            if (m_tot_calls > 1 || (m_tot_calls == 1 && m_mode == PrimeKeyDerivationMode::PRIME)) {
                for (int k = 0; k < (m_mode == PrimeKeyDerivationMode::PRIME ? m_tot_calls : m_tot_calls - 1); ++k) {
                    key_val = euler_totient(key_val);
                }
            }

            int shift = (m_operation == PrimeOperation::ADD) ? (key_val % 29) : (-(key_val % 29));
            int current_idx = static_cast<int>(indices[i]);
            indices[i] = static_cast<uint8_t>((current_idx + shift + 29) % 29);

            prime_ptr++;
            rune_pos++;
        }
    }

    int PrimeStreamTransformer::get_nth_prime(size_t n) {
        if (n < G_PRIMES.size()) return G_PRIMES[n];
        int p = G_PRIMES.empty() ? 2 : G_PRIMES.back();
        for (size_t i = G_PRIMES.size(); i <= n; ++i) p = find_next_prime(p);
        return p;
    }

    MobiusTransformer::MobiusTransformer(bool add, const std::vector<size_t>& interrupt_indices)
        : m_add(add)
    {
        for (auto idx : interrupt_indices) m_interrupts.insert(idx);
    }

    void MobiusTransformer::transform(ProcessedText& pt)
    {
        auto& indices = pt.indices();
        size_t actual_rune_idx = 0;
        int n = 1;
        for (size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] >= 29) continue;
            if (m_interrupts.count(actual_rune_idx)) {
                actual_rune_idx++; n++; continue;
            }
            int shift = mobius(n);
            if (!m_add) shift = -shift;
            int val = static_cast<int>(indices[i]);
            {
                int tmp = (static_cast<int>(val) + static_cast<int>(shift) + 29) % 29;
                indices[i] = static_cast<uint8_t>(tmp);
            }
            actual_rune_idx++; n++;
        }
    }

    MobiusHypothesisTransformer::MobiusHypothesisTransformer(int shift_val, const std::vector<int>& offsets, MathArgumentMode mode, int global_word_count)
        : m_shift_val(shift_val), m_offsets(offsets), m_mode(mode), m_global_word_start(global_word_count) {}

    void MobiusHypothesisTransformer::transform(ProcessedText& pt)
    {
        auto& indices = pt.indices();
        int word_counter = 1;
        int section_word_counter = 1;
        
        size_t i = 0;
        while (i < indices.size()) {
            // Find end of current word
            size_t word_end = i;
            while (word_end < indices.size() && indices[word_end] < 29) {
                word_end++;
            }

            // Rhythmic offset: uses sequence offset based on word counter
            int current_offset = m_offsets[word_counter % m_offsets.size()];

            int n_arg = 0;
            switch(m_mode) {
                case MathArgumentMode::WORD_INDEX: 
                    n_arg = (m_global_word_start > 0) ? 
                            (m_global_word_start + word_counter - 1 + current_offset) : 
                            (word_counter + current_offset);
                    break;
                case MathArgumentMode::SECTION_WORD: // n = word within the section (reset at §)
                    n_arg = section_word_counter + current_offset; break;
                case MathArgumentMode::GP_SUM: {
                    int sum = 0;
                    for(size_t k=i; k<word_end; ++k) if(indices[k] < 29) sum += indices[k];
                    n_arg = sum + current_offset;
                    break;
                }
                case MathArgumentMode::PAGE_INDEX: // n = page index (1, 2...)
                    n_arg = static_cast<int>(pt.page_index()) + current_offset; break; 
                default: n_arg = word_counter + current_offset; 
            } // End switch
            
            int m = mobius(n_arg);
            
            for (size_t j = i; j < word_end; ++j) {
                if (indices[j] >= 29) continue;

                int val = static_cast<int>(indices[j]);
                int shift = 0;
                if (m == 1) {
                    shift = m_shift_val; // Aditiva
                } else if (m == -1) {
                    shift = -m_shift_val; // Subtrativa
                } else {
                    shift = 0; // Direta (M=0)
                }
                {
                    int tmp = (static_cast<int>(val) + static_cast<int>(shift) + 29) % 29;
                    indices[j] = static_cast<uint8_t>(tmp);
                }
            }

            // Section advance and reset logic
            if (word_end < indices.size()) {
                if (indices[word_end] == SPECIAL_SECTION_IDX) {
                    section_word_counter = 1; // Reset at §
                }
                
                if (indices[word_end] == SPECIAL_HYPHEN_IDX || indices[word_end] == SPECIAL_SPACE_IDX || 
                    indices[word_end] == SPECIAL_SECTION_IDX || indices[word_end] == SPECIAL_PERIOD_IDX ||
                    indices[word_end] == SPECIAL_AMPERSAND_IDX || indices[word_end] == SPECIAL_SLASH_IDX) {
                    word_counter++;
                    section_word_counter++;
                }
                i = word_end + 1; 
            } else {
                break;
            }
        }
    }

    // --- Transposition Transformer ---

    TranspositionTransformer::TranspositionTransformer(const std::vector<size_t>& key_permutation)
        : m_key_permutation(key_permutation)
    {
        if (!m_key_permutation.empty()) {
            // Validation: Check if it is a valid permutation from 0 to N-1
            std::vector<size_t> check = m_key_permutation;
            std::sort(check.begin(), check.end());
            for (size_t i = 0; i < check.size(); ++i) {
                if (check[i] != i) {
                    m_key_permutation.clear();
                    break;
                }
            }
        }
    }

    void TranspositionTransformer::transform(ProcessedText& pt)
    {
        if (m_key_permutation.empty()) return;
        auto& indices = pt.indices();
        std::vector<size_t> rune_positions;
        std::vector<uint8_t> runes_only;

        for (size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] < 29) {
                rune_positions.push_back(i);
                runes_only.push_back(indices[i]);
            }
        }

        size_t L = runes_only.size();
        size_t K = m_key_permutation.size();
        if (L == 0 || K == 0) return;
        size_t R = (L + K - 1) / K;

        std::vector<uint8_t> transposed(L);
        size_t write_idx = 0;

        // Columnar Reading
        for (size_t col_idx : m_key_permutation) {
            for (size_t row = 0; row < R; ++row) {
                size_t src_idx = row * K + col_idx;
                if (src_idx < L) {
                    transposed[write_idx++] = runes_only[src_idx];
                }
            }
        }

        for (size_t i = 0; i < L; ++i) {
            indices[rune_positions[i]] = transposed[i];
        }
    }

    ShiftTransformer::ShiftTransformer(int shift) : m_shift(shift) {}

    void ShiftTransformer::transform(ProcessedText& pt)
    {
        auto& indices = pt.indices();
        for (auto& idx : indices)
        {
            if (idx < 29) {
                {
                    int tmp = (static_cast<int>(idx) + static_cast<int>(m_shift) + 29) % 29;
                    idx = static_cast<uint8_t>(tmp);
                }
            }
        }
    }

    AtbashTransformer::AtbashTransformer(int shift) : m_shift(shift) {}

    void AtbashTransformer::transform(ProcessedText& pt)
    {
        auto& indices = pt.indices();
        for (auto& idx : indices)
        {
            if (idx < 29)
            {
                {
                    int tmp = (28 - static_cast<int>(idx) + static_cast<int>(m_shift) + 29) % 29;
                    idx = static_cast<uint8_t>(tmp);
                }
            }
        }
    }

    // --- SkipVigenereTransformer ---

    SkipVigenereTransformer::SkipVigenereTransformer(const std::string& vigenere_key_utf8, 
                                                   const std::vector<int>& skip_deltas,
                                                   size_t first_skip_index)
    {
        auto maybe_indices = to_rune_indices(vigenere_key_utf8);
        if (maybe_indices) m_key_indices = *maybe_indices;
        
        // Converts deltas (distances) into absolute rune positions
        size_t current = first_skip_index;
        m_interrupts.insert(current);
        for (int d : skip_deltas) {
            current += d;
            m_interrupts.insert(current);
        }
    }

    void SkipVigenereTransformer::transform(ProcessedText& pt)
    {
        if (m_key_indices.empty()) return;
        auto& indices = pt.indices();
        size_t rune_pos = 0;   // Contador de runas (ignora espaços/hífens)
        size_t key_ptr = 0;    // Ponteiro na chave Vigenère

        for (size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] >= 29) continue; // Skips special characters

            if (m_interrupts.count(rune_pos)) {
                rune_pos++; // Skip position: advance rune count but not key pointer
                continue;
            }

            int val = static_cast<int>(indices[i]);
            int key = static_cast<int>(m_key_indices[key_ptr % m_key_indices.size()]);
                {
                    int tmp = (static_cast<int>(val) - static_cast<int>(key) + 29) % 29;
                    indices[i] = static_cast<uint8_t>(tmp);
                }
            
            key_ptr++;
            rune_pos++;
        }
    }

    // --- RhythmicVigenereTransformer ---

    RhythmicVigenereTransformer::RhythmicVigenereTransformer(const std::string& vigenere_key_utf8, 
                                                           const std::vector<int>& rhythmic_deltas,
                                                           size_t first_skip_index)
        : m_deltas(rhythmic_deltas), m_first_skip(first_skip_index)
    {
        auto maybe_indices = to_rune_indices(vigenere_key_utf8);
        if (maybe_indices) m_key_indices = *maybe_indices;
    }

    void RhythmicVigenereTransformer::transform(ProcessedText& pt)
    {
        if (m_key_indices.empty()) return;
        auto& indices = pt.indices();
        size_t rune_pos = 0;   
        size_t key_ptr = 0;    
        size_t delta_ptr = 0;
        size_t next_skip_target = m_first_skip;

        for (size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] >= 29) continue; 

            // Rhythmic interruption point reached
            if (rune_pos == next_skip_target && !m_deltas.empty()) {
                next_skip_target += m_deltas[delta_ptr % m_deltas.size()];
                delta_ptr++;
                rune_pos++; // Consumes rune position (noise)
                continue;   // DO NOT consume Vigenère key
            }

            int val = static_cast<int>(indices[i]);
            int key = static_cast<int>(m_key_indices[key_ptr % m_key_indices.size()]);
            {
                int tmp = (static_cast<int>(val) - static_cast<int>(key) + 29) % 29;
                indices[i] = static_cast<uint8_t>(tmp);
            }
            
            key_ptr++;
            rune_pos++;
        }
    }

    // --- HybridRhythmicVigenereTransformer ---

    HybridRhythmicVigenereTransformer::HybridRhythmicVigenereTransformer(
        const std::string& vigenere_key_utf8, 
        const std::vector<size_t>& fixed_interrupts,
        const std::vector<int>& rhythmic_deltas,
        size_t first_skip_index)
        : m_fixed_interrupts(fixed_interrupts.begin(), fixed_interrupts.end()),
          m_deltas(rhythmic_deltas), m_first_skip(first_skip_index)
    {
        auto maybe_indices = to_rune_indices(vigenere_key_utf8);
        if (maybe_indices) m_key_indices = *maybe_indices;
    }

    void HybridRhythmicVigenereTransformer::transform(ProcessedText& pt)
    {
        if (m_key_indices.empty()) return;
        auto& indices = pt.indices();
        size_t rune_pos = 0;   
        size_t key_ptr = 0;    
        size_t delta_ptr = 0;
        size_t next_rhythmic_skip = m_first_skip;

        for (size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] >= 29) continue; 

            bool is_fixed = m_fixed_interrupts.count(rune_pos);
            bool is_rhythmic = (rune_pos == next_rhythmic_skip && !m_deltas.empty());

            if (is_fixed || is_rhythmic) {
                if (is_rhythmic) {
                    next_rhythmic_skip += m_deltas[delta_ptr % m_deltas.size()];
                    delta_ptr++;
                }
                rune_pos++;
                continue;
            }

            int val = static_cast<int>(indices[i]);
            int key = static_cast<int>(m_key_indices[key_ptr % m_key_indices.size()]);
            {
                int tmp = (static_cast<int>(val) - static_cast<int>(key) + 29) % 29;
                indices[i] = static_cast<uint8_t>(tmp);
            }
            
            key_ptr++;
            rune_pos++;
        }
    }

    // --- AutokeyTransformer ---

    AutokeyTransformer::AutokeyTransformer(const std::string& utf8_seed, AutokeyMode mode)
        : m_mode(mode)
    {
        auto maybe_indices = to_rune_indices(utf8_seed);
        if (maybe_indices) m_seed_indices = *maybe_indices;
    }

    void AutokeyTransformer::transform(ProcessedText& pt)
    {
        if (m_seed_indices.empty()) return;
        auto& indices = pt.indices();
        size_t seed_len = m_seed_indices.size();
        
        std::vector<uint8_t> key_history; 
        key_history.reserve(indices.size());

        for (size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] >= 29) continue;

            int current_cipher = static_cast<int>(indices[i]);
            int key_val = (key_history.size() < seed_len) 
                          ? static_cast<int>(m_seed_indices[key_history.size()])
                          : static_cast<int>(key_history[key_history.size() - seed_len]);

            int tmp = (static_cast<int>(current_cipher) - static_cast<int>(key_val) + 29) % 29;
            uint8_t p = static_cast<uint8_t>(tmp);

            if (m_mode == AutokeyMode::PLAINTEXT) key_history.push_back(p);
            else key_history.push_back(indices[i]);

            indices[i] = p;
        }
    }

} // namespace core