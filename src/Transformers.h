#ifndef TRANSFORMERS_H
#define TRANSFORMERS_H

#include "ProcessedText.h"
#include <vector>
#include <set>
#include <string>

namespace core
{
    struct OEISSequence
    {
        std::string id;
        std::vector<int> data;
    };

    enum class MathArgumentMode {
    RUNE_INDEX,      // n = 1, 2, 3... per rune
    WORD_INDEX,      // n = 1, 2, 3... per word
    GP_SUM,          // n = sum of runic indices of the word
    PAGE_INDEX,      // n = page index (1, 2...)
    SECTION_WORD     // n = word within the section (resets on §)
    };

    enum class AutokeyMode {
        PLAINTEXT,
        CIPHERTEXT
    };

    // Base interface for all transformers
    class Transformer
    {
    public:
        virtual ~Transformer() = default;
        virtual void transform(ProcessedText& pt) = 0;
    };

    // -----------------------------------------------------------------------
    // PrimeStreamTransformer: Applies shifts based on a stream of prime numbers
    // -----------------------------------------------------------------------
    enum class PrimeKeyDerivationMode {
        PRIME_MINUS_ONE,        // Key = (prime - 1) % 29
        PRIME,                  // Key = prime % 29
        PRIME_PLUS_ONE,         // Key = (prime + 1) % 29
        TOTIENT_PRIME_MINUS_ONE, // Key = φ(prime - 1) % 29
        TOTIENT_PRIME           // Key = φ(prime) % 29
    };

    enum class PrimeOperation {
        ADD,
        SUBTRACT
    };

    class PrimeStreamTransformer : public Transformer {
    public:
        PrimeStreamTransformer(PrimeOperation op, PrimeKeyDerivationMode mode, 
                               size_t initial_prime_idx = 0, 
                               const std::set<size_t>& custom_interrupts = {},
                               int tot_calls = 1, bool emirp = false);
        void transform(ProcessedText& pt) override;
    private:
        PrimeOperation m_operation;
        PrimeKeyDerivationMode m_mode;
        size_t m_initial_prime_idx;
        std::set<size_t> m_custom_interrupts;
        int m_tot_calls;
        bool m_emirp;
        static int get_nth_prime(size_t n); // Uses G_PRIMES
    };

    // -----------------------------------------------------------------------
    // AtbashTransformer: idx -> (28 - idx + shift) % 29
    // -----------------------------------------------------------------------
    class AtbashTransformer : public Transformer
    {
    public:
        explicit AtbashTransformer(int shift = 0);
        void transform(ProcessedText& pt) override;
    private:
        int m_shift;
    };

    // -----------------------------------------------------------------------
    // ShiftTransformer: idx -> (idx + shift) % 29  (simple Caesar)
    // FIX: implemented in Transformers.cpp but previously missing header declaration
    // -----------------------------------------------------------------------
    class ShiftTransformer : public Transformer
    {
    public:
        explicit ShiftTransformer(int shift);
        void transform(ProcessedText& pt) override;
    private:
        int m_shift;
    };

    // -----------------------------------------------------------------------
    // VigenereTransformer: rune UTF-8 key; supports interruption positions
    // FIX: duplicated class variants existed in original header (m_key string
    //      vs. m_key_indices). Only the correct implementation (m_key_indices)
    //      is kept (matches Transformers.cpp).
    // -----------------------------------------------------------------------
    class VigenereTransformer : public Transformer
    {
    public:
        // utf8_key: chave como string de runas UTF-8
        // interrupt_indices: posições de runa (0-based no contador de runas)
        //                    que são puladas sem consumir a chave
        VigenereTransformer(const std::string& utf8_key,
                            const std::vector<size_t>& interrupt_indices = {});
        void transform(ProcessedText& pt) override;
    private:
        std::vector<uint8_t> m_key_indices;
        std::set<size_t>     m_interrupts;
    };

    // -----------------------------------------------------------------------
    // SequenceTransformer: aplica sequência inteira (ex: OEIS) como chave
    // -----------------------------------------------------------------------
    class SequenceTransformer : public Transformer
    {
    public:
        SequenceTransformer(const std::vector<int>& sequence,
                            const std::vector<size_t>& interrupt_indices = {});

        void transform(ProcessedText& pt) override;

    // Load sequences from a CSV file (OEIS format: id,v1,v2,...)
        static std::vector<OEISSequence> load_oeis_file(const std::string& filename);

    private:
        std::vector<int> m_sequence;
        std::set<size_t> m_interrupts;
    };

    // -----------------------------------------------------------------------
    // MobiusTransformer: shift = μ(n) per rune position
    // -----------------------------------------------------------------------
    class MobiusTransformer : public Transformer
    {
    public:
        MobiusTransformer(bool add = false,
                          const std::vector<size_t>& interrupt_indices = {});
        void transform(ProcessedText& pt) override;

    private:
        bool             m_add;
        std::set<size_t> m_interrupts;
    };

    // -----------------------------------------------------------------------
    // MobiusHypothesisTransformer: shift = ±shift_val based on μ(n_arg)
    //   where n_arg depends on MathArgumentMode
    // -----------------------------------------------------------------------
    class MobiusHypothesisTransformer : public Transformer
    {
    public:
        MobiusHypothesisTransformer(int shift_val,
                                    const std::vector<int>& offsets,
                                    MathArgumentMode mode = MathArgumentMode::WORD_INDEX,
                                    int global_word_count = 0);
        void transform(ProcessedText& pt) override;
    private:
        int              m_shift_val;
        std::vector<int> m_offsets;
        MathArgumentMode m_mode;
        int              m_global_word_start;
    };

    // -----------------------------------------------------------------------
    // TranspositionTransformer: columnar transposition of runes
    // -----------------------------------------------------------------------
    class TranspositionTransformer : public Transformer
    {
    public:
        // key_permutation: e.g., {2, 0, 1} para 3 colunas (lê col 2, depois 0, depois 1)
        explicit TranspositionTransformer(const std::vector<size_t>& key_permutation);
        void transform(ProcessedText& pt) override;

    private:
        std::vector<size_t> m_key_permutation;
    };

    // -----------------------------------------------------------------------
    // SkipVigenereTransformer: Vigenere that skips runes based on deltas
    // -----------------------------------------------------------------------
    class SkipVigenereTransformer : public Transformer
    {
    public:
    // vigenere_key_utf8: textual key (e.g. "DIVINITY")
    // skip_deltas: distances between skips (e.g. 9, 33, 1...)
    // first_skip_index: position of the first rune to be skipped (default 6)
        SkipVigenereTransformer(const std::string& vigenere_key_utf8, 
                               const std::vector<int>& skip_deltas,
                               size_t first_skip_index = 6);
        void transform(ProcessedText& pt) override;
    private:
        std::vector<uint8_t> m_key_indices;
        std::set<size_t>     m_interrupts;
    };

    // -----------------------------------------------------------------------
    // RhythmicVigenereTransformer: Vigenere with cyclic skips (deltas)
    // -----------------------------------------------------------------------
    class RhythmicVigenereTransformer : public Transformer
    {
    public:
        RhythmicVigenereTransformer(const std::string& vigenere_key_utf8, 
                                   const std::vector<int>& rhythmic_deltas,
                                   size_t first_skip_index = 0);
        void transform(ProcessedText& pt) override;
    private:
        std::vector<uint8_t> m_key_indices;
        std::vector<int>     m_deltas;
        size_t               m_first_skip;
    };

    // -----------------------------------------------------------------------
    // HybridRhythmicVigenereTransformer: Combines fixed interruptions (positions)
    // with rhythmic deltas. Useful for pages mixing static and dynamic noise.
    // -----------------------------------------------------------------------
    class HybridRhythmicVigenereTransformer : public Transformer
    {
    public:
        HybridRhythmicVigenereTransformer(const std::string& vigenere_key_utf8, 
                                         const std::vector<size_t>& fixed_interrupts,
                                         const std::vector<int>& rhythmic_deltas,
                                         size_t first_skip_index = 0);
        void transform(ProcessedText& pt) override;
    private:
        std::vector<uint8_t> m_key_indices;
        std::set<size_t>     m_fixed_interrupts;
        std::vector<int>     m_deltas;
        size_t               m_first_skip;
    };

    // -----------------------------------------------------------------------
    // AutokeyTransformer: cipher where the key is the plaintext itself (Plaintext Autokey)
    // P[i] = (C[i] - K[i]) mod 29
    // For i < len(key): K[i] = key[i]
    // For i >= len(key): K[i] = P[i - len(key)]
    // -----------------------------------------------------------------------
    class AutokeyTransformer : public Transformer
    {
    public:
        // utf8_seed: A palavra inicial que "dispara" a autokey
        explicit AutokeyTransformer(const std::string& utf8_seed, 
                                   AutokeyMode mode = AutokeyMode::PLAINTEXT);
        void transform(ProcessedText& pt) override;

    private:
        std::vector<uint8_t> m_seed_indices;
        AutokeyMode          m_mode;
    };

} // namespace core

#endif // TRANSFORMERS_H