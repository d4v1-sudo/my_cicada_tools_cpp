#ifndef PROCESSED_TEXT_H
#define PROCESSED_TEXT_H

#include <vector>
#include <string>
#include <string_view>
#include <array>
#include <cstdint>
#include <map>

namespace core
{
    class ProcessedText
    {
    public:
        ProcessedText() = default;

        // Constrói a partir de uma string de runas UTF-8
        explicit ProcessedText(std::string_view utf8_runes, size_t page_idx = 0);

        // Converte para string legível
        std::string to_latin() const;
        std::string to_runes() const;
        std::vector<uint16_t> to_primes() const;

        // Estatísticas
        double index_of_coincidence() const;
        double latin_index_of_coincidence() const;
        double entropy() const;
        double chi_square_uniform() const;

        // Análise de N-gramas
        double bigram_index_of_coincidence() const;

        // Análises avançadas
        std::vector<double> autocorrelation() const;
        double periodic_ioc(size_t period) const;
        std::map<std::vector<uint8_t>, std::vector<size_t>> kasiski_examination(size_t seq_len) const;

        std::array<double, 29> runic_distribution() const;
        std::vector<double> bigram_distribution() const;
        std::vector<double> trigram_distribution() const;
        std::vector<double> transition_matrix() const; // Matriz 29x29 achatada
        std::array<double, 26> latin_distribution() const;

        // Utilitários de segmentação de palavras
        std::vector<std::vector<uint8_t>> get_words() const;
        int calculate_gp_sum(const std::vector<uint8_t>& word) const;

        // Acesso direto aos índices para os Transformers
        std::vector<uint8_t>& indices() { return m_indices; }
        const std::vector<uint8_t>& indices() const { return m_indices; }

        size_t size() const { return m_indices.size(); }
        size_t rune_count() const {
            size_t c = 0;
            for (auto x : m_indices) if (x < 29) c++;
            return c;
        }
        size_t page_index() const { return m_page_index; }

    private:
        std::vector<uint8_t> m_indices;
        size_t m_page_index = 0; // FIX: membro estava ausente no header original
    };
}

#endif // PROCESSED_TEXT_H