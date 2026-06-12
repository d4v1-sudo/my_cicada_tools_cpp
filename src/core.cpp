#include "core.h"
#include <unordered_map>
#include <stdexcept>
#include <sstream>
#include <cmath>
#include <fstream>
#include <array>
#include <algorithm>
#include <iostream>

namespace core
{

// RuneDB — internal implementation using the Pimpl idiom for encapsulation and to avoid static initialization order issues.
struct RuneDB::Impl
{
    std::unordered_map<std::string_view, size_t> rune_to_index;
    std::unordered_map<std::string_view, size_t> latin_to_index;

    Impl()
    {
        for (size_t i = 0; i < RUNE_TABLE.size(); ++i)
        {
            rune_to_index[RUNE_TABLE[i].rune] = i;
            latin_to_index[RUNE_TABLE[i].latin] = i;
        }
    }
};

RuneDB::RuneDB() : impl_(std::make_unique<Impl>())
{
    // Impl() constructor runs here, populating the lookup maps.
}

RuneDB::~RuneDB() = default;

RuneDB& RuneDB::instance()
{
    static RuneDB inst;
    return inst;
}

std::optional<std::string_view> RuneDB::rune_to_latin(std::string_view rune) const
{
    auto it = impl_->rune_to_index.find(rune);
    if (it == impl_->rune_to_index.end()) return std::nullopt;
    return RUNE_TABLE[it->second].latin;
}

std::optional<std::string_view> RuneDB::latin_to_rune(std::string_view latin) const
{
    auto it = impl_->latin_to_index.find(latin);
    if (it == impl_->latin_to_index.end()) return std::nullopt;
    return RUNE_TABLE[it->second].rune;
}

std::optional<uint16_t> RuneDB::rune_to_prime(std::string_view rune) const
{
    auto it = impl_->rune_to_index.find(rune);
    if (it == impl_->rune_to_index.end()) return std::nullopt;
    return RUNE_TABLE[it->second].prime;
}

std::optional<uint16_t> RuneDB::latin_to_prime(std::string_view latin) const
{
    auto it = impl_->latin_to_index.find(latin);
    if (it == impl_->latin_to_index.end()) return std::nullopt;
    return RUNE_TABLE[it->second].prime;
}

std::optional<const RuneEntry*> RuneDB::by_index(size_t idx) const noexcept
{
    if (idx >= RUNE_TABLE.size()) return std::nullopt;
    return &RUNE_TABLE[idx];
}

std::optional<size_t> RuneDB::get_index_rune(std::string_view rune) const
{
    auto it = impl_->rune_to_index.find(rune);
    return (it != impl_->rune_to_index.end()) ? std::optional<size_t>(it->second) : std::nullopt;
}

std::optional<size_t> RuneDB::get_index_latin(std::string_view latin) const
{
    auto it = impl_->latin_to_index.find(latin);
    return (it != impl_->latin_to_index.end()) ? std::optional<size_t>(it->second) : std::nullopt;
}

// --- Mathematical functions for cryptanalysis ---

long long gcd(long long a, long long b) {
    while (b) {
        a %= b;
        std::swap(a, b);
    }
    return a;
}

int euler_totient(int n) {
    if (n <= 0) return 0;
    int result = n;
    int tmp = n;
    for (int i = 2; i * i <= tmp; i++) {
        if (tmp % i == 0) {
            while (tmp % i == 0) tmp /= i;
            result -= result / i;
        }
    }
    if (tmp > 1) result -= result / tmp;
    return result;
}

bool is_prime(int n) {
    if (n <= 1) return false;
    if (n <= 3) return true;
    if (n % 2 == 0 || n % 3 == 0) return false;
    for (int i = 5; i * i <= n; i += 6)
        if (n % i == 0 || n % (i + 2) == 0) return false;

    return true;
}

int find_next_prime(int n) {
    int p = n + 1;
    while (!is_prime(p)) p++;
    return p;
}

int reverse_digits(int n) {
    int rev = 0;
    while (n > 0) {
        rev = rev * 10 + (n % 10);
        n /= 10;
    }
    return rev;
}

int mobius(int n) {
    if (n <= 0) return 0;
    if (n == 1) return 1;
    int factors = 0;
    for (int i = 2; i * i <= n; i++) {
        if (n % i == 0) {
            n /= i;
            factors++;
            if (n % i == 0) return 0;
        }
    }
    if (n > 1) factors++;
    return (factors % 2 == 0) ? 1 : -1;
}

double kullback_leibler_divergence(const std::array<double, 29>& p, const std::array<double, 29>& q) {
    double div = 0.0;
    for (size_t i = 0; i < 29; ++i) {
        if (p[i] > 0 && q[i] > 0) {
            div += p[i] * std::log(p[i] / q[i]);
        }
    }
    return div;
}

void save_solution(const Solution& sol) {
    std::ofstream file("../output/solutions_found.txt", std::ios::app);
    if (file.is_open()) {
        file << "=== [NEW DISCOVERY] SOLUTION FOR: " << sol.page_name << " ===\n";
        file << "Method: " << sol.transformer_type << "\n";
        file << "Params: " << sol.parameters << "\n";
        file << "Decrypted:\n" << sol.full_decrypted_text << "\n";
        file << "------------------------------------------\n\n";
        std::wcout << L"\n[DEBUG] Solution for " << std::wstring(sol.page_name.begin(), sol.page_name.end()) 
                   << L" written to ../output/solutions_found.txt\n";
    }
}

// --- API Livre ---

void init_tables()
{
    // Força a criação da instância estática
    RuneDB::instance();
}

std::optional<std::string_view> to_latin(std::string_view rune)
{
    return RuneDB::instance().rune_to_latin(rune);
}

std::optional<std::string_view> to_rune(std::string_view latin)
{
    return RuneDB::instance().latin_to_rune(latin);
}

std::optional<uint16_t> to_prime(std::string_view rune)
{
    return RuneDB::instance().rune_to_prime(rune);
}

// --- ProcessedText Implementation ---

ProcessedText::ProcessedText(std::string_view utf8_runes, size_t page_idx) : m_page_index(page_idx)
{
    for (size_t i = 0; i < utf8_runes.size(); )
    {
        unsigned char first = static_cast<unsigned char>(utf8_runes[i]);
        size_t char_len = 1;
        if      ((first & 0x80) == 0x00) char_len = 1;
        else if ((first & 0xE0) == 0xC0) char_len = 2;
        else if ((first & 0xF0) == 0xE0) char_len = 3;
        else if ((first & 0xF8) == 0xF0) char_len = 4;

        if (i + char_len > utf8_runes.size()) char_len = 1;

        std::string_view token = utf8_runes.substr(i, char_len);
        auto rune_idx = RuneDB::instance().get_index_rune(token);

        if (rune_idx) {
            m_indices.push_back(static_cast<uint8_t>(*rune_idx));
        } else if (token == "-") { m_indices.push_back(SPECIAL_HYPHEN_IDX); }
        else if (token == ".") { m_indices.push_back(SPECIAL_PERIOD_IDX); }
        else if (token == "&") { m_indices.push_back(SPECIAL_AMPERSAND_IDX); }
        else if (token == "$") { m_indices.push_back(SPECIAL_DOLLAR_IDX); }
        else if (token == "§") { m_indices.push_back(SPECIAL_SECTION_IDX); }
        else if (token == "/") { m_indices.push_back(SPECIAL_SLASH_IDX); }
        else if (token == "%") { m_indices.push_back(SPECIAL_PERCENT_IDX); }
        else if (char_len == 1) {
            char c = token[0];
            if      (c == ' ')             m_indices.push_back(SPECIAL_SPACE_IDX);
            else if (c == '\n')            m_indices.push_back(SPECIAL_NEWLINE_IDX);
            else if (isdigit(c)) {
                uint8_t val = static_cast<uint8_t>(SPECIAL_DIGIT_OFFSET + (c - '0'));
                m_indices.push_back(val);
            }
            else if (c >= 'a' && c <= 'f') {
                uint8_t val = static_cast<uint8_t>(SPECIAL_HEX_LETTER_OFFSET + (c - 'a'));
                m_indices.push_back(val);
            }
            else                           m_indices.push_back(SPECIAL_UNKNOWN_CHAR_IDX);
        } else {
            m_indices.push_back(SPECIAL_UNKNOWN_CHAR_IDX);
        }
        i += char_len;
    }
}

std::vector<std::vector<uint8_t>> ProcessedText::get_words() const {
    std::vector<std::vector<uint8_t>> words;
    std::vector<uint8_t> current_word;
    for (auto idx : m_indices) {
        if (idx < 29) {
            current_word.push_back(idx);
        } else if (!current_word.empty()) {
            words.push_back(std::move(current_word));
        }
    }
    if (!current_word.empty()) words.push_back(current_word);
    return words;
}

int ProcessedText::calculate_gp_sum(const std::vector<uint8_t>& word) const {
    int sum = 0;
    for (uint8_t idx : word) sum += static_cast<int>(idx);
    return sum;
}

std::string ProcessedText::to_latin() const
{
    std::string res;
    res.reserve(m_indices.size() * 2); // Reserva espaço suficiente
    for (auto idx : m_indices) {
        if (idx < 29) { // É uma runa
            res += RUNE_TABLE[idx].latin;
        } else if (idx == SPECIAL_HYPHEN_IDX || idx == SPECIAL_SPACE_IDX) {
            res += ' ';
        } else if (idx == SPECIAL_PERIOD_IDX) {
            res += '.';
        } else if (idx == SPECIAL_AMPERSAND_IDX) {
            res += '&';
        } else if (idx == SPECIAL_DOLLAR_IDX) {
            res += '$';
        } else if (idx == SPECIAL_SECTION_IDX) { // '§' is multi-byte UTF-8
            res += "\xC2\xA7"; 
        } else if (idx == SPECIAL_SLASH_IDX) {
            res += '/';
        } else if (idx == SPECIAL_PERCENT_IDX) {
            res += '%';
        } else if (idx == SPECIAL_NEWLINE_IDX) {
            res += '\n';
        } else if (idx >= SPECIAL_DIGIT_OFFSET && idx <= SPECIAL_DIGIT_OFFSET + 9) {
            res += (char)('0' + (idx - SPECIAL_DIGIT_OFFSET));
        } else if (idx >= SPECIAL_HEX_LETTER_OFFSET && idx <= SPECIAL_HEX_LETTER_OFFSET + 5) {
            res += (char)('a' + (idx - SPECIAL_HEX_LETTER_OFFSET));
        } else {
            res += '?'; // character unknown
        }
    }
    return res;
}

std::string ProcessedText::to_runes() const
{
    std::string res;
    res.reserve(m_indices.size() * 4); // reserving more space for multi-byte runes
    for (auto idx : m_indices) {
        if (idx < 29) { // É uma runa
            res += RUNE_TABLE[idx].rune;
        } else if (idx == SPECIAL_HYPHEN_IDX) {
            res += '-';
        } else if (idx == SPECIAL_PERIOD_IDX) {
            res += '.';
        } else if (idx == SPECIAL_AMPERSAND_IDX) {
            res += '&';
        } else if (idx == SPECIAL_DOLLAR_IDX) {
            res += '$';
        } else if (idx == SPECIAL_SECTION_IDX) { // '§' is multi-byte UTF-8
            res += "\xC2\xA7";
        } else if (idx == SPECIAL_SLASH_IDX) {
            res += '/';
        } else if (idx == SPECIAL_PERCENT_IDX) {
            res += '%';
        } else if (idx == SPECIAL_SPACE_IDX) {
            res += ' ';
        } else if (idx == SPECIAL_NEWLINE_IDX) {
            res += '\n';
        } else if (idx >= SPECIAL_DIGIT_OFFSET && idx <= SPECIAL_DIGIT_OFFSET + 9) {
            res += (char)('0' + (idx - SPECIAL_DIGIT_OFFSET));
        } else if (idx >= SPECIAL_HEX_LETTER_OFFSET && idx <= SPECIAL_HEX_LETTER_OFFSET + 5) {
            res += (char)('a' + (idx - SPECIAL_HEX_LETTER_OFFSET));
        } else {
            res += '?'; // character unknown
        }
    }
    return res;
}

std::vector<uint16_t> ProcessedText::to_primes() const
{
    std::vector<uint16_t> res;
    res.reserve(m_indices.size());
    for (auto idx : m_indices) {
        if (idx < 29) { // É uma runa
            res.push_back(RUNE_TABLE[idx].prime);
        } else {
            res.push_back(0);
        }
    }
    return res;
}

double ProcessedText::index_of_coincidence() const
{
    if (m_indices.size() <= 1) return 0.0;

    std::array<size_t, 29> freqs{};
    size_t count = 0;
    for (auto idx : m_indices) if (idx < 29) { freqs[idx]++; count++; }

    if (count <= 1) return 0.0;

    double sum = 0.0;
    for (size_t f : freqs) {
        double df = static_cast<double>(f);
        sum += df * (df - 1.0);
    }

    double N = static_cast<double>(count);
    return sum / (N * (N - 1.0));
}

double ProcessedText::latin_index_of_coincidence() const
{
    std::array<size_t, 26> freqs{};
    size_t total_chars = 0;
    for (auto idx : m_indices) {
        if (idx >= RUNE_TABLE.size()) continue;
        std::string_view l = RUNE_TABLE[idx].latin;
        for (char c : l) {
            if (c >= 'A' && c <= 'Z') {
                freqs[c - 'A']++;
                total_chars++;
            }
        }
    }
    if (total_chars <= 1) return 0.0;
    double sum = 0.0;
    for (size_t f : freqs) { double df = static_cast<double>(f); sum += df * (df - 1.0); }
    double N = static_cast<double>(total_chars);
    return sum / (N * (N - 1.0));
}

double ProcessedText::entropy() const
{
    std::array<size_t, 29> freqs{};
    size_t count = 0;
    for (auto idx : m_indices) if (idx < 29) { freqs[idx]++; count++; }

    if (count == 0) return 0.0;

    double ent = 0.0;
    double N = static_cast<double>(count);
    for (size_t f : freqs) {
        if (f > 0) {
            double p = static_cast<double>(f) / N;
            ent -= p * std::log2(p);
        }
    }
    return ent;
}

double ProcessedText::chi_square_uniform() const
{
    std::array<size_t, 29> freqs{};
    size_t count = 0;
    for (auto idx : m_indices) if (idx < 29) { freqs[idx]++; count++; }

    if (count == 0) return 0.0;

    double N = static_cast<double>(count);
    double expected = N / 29.0;
    double chi = 0.0;
    for (size_t f : freqs) {
        double diff = static_cast<double>(f) - expected;
        chi += (diff * diff) / expected;
    }
    return chi;
}

double ProcessedText::bigram_index_of_coincidence() const
{
    std::vector<uint8_t> clean;
    for (auto idx : m_indices) if (idx < 29) clean.push_back(idx);
    if (clean.size() < 2) return 0.0;
    
    // 29 * 29 = 841 combinações possíveis
    std::vector<size_t> freqs(841, 0);
    for (size_t i = 0; i < clean.size() - 1; ++i) {
        size_t bigram_idx = static_cast<size_t>(clean[i]) * 29 + static_cast<size_t>(clean[i+1]);
        freqs[bigram_idx]++;
    }

    double sum = 0.0;
    for (size_t f : freqs) {
        if (f > 1) {
            double df = static_cast<double>(f);
            sum += df * (df - 1.0);
        }
    }

    double N = static_cast<double>(clean.size() - 1);
    if (N <= 1.0) return 0.0;
    
    return sum / (N * (N - 1.0));
}

std::vector<double> ProcessedText::autocorrelation() const
{
    std::vector<uint8_t> clean;
    for (auto idx : m_indices) if (idx < 29) clean.push_back(idx);

    size_t max_shift = std::min(clean.size() / 2, (size_t)50);
    std::vector<double> shifts(max_shift, 0.0);

    for (size_t s = 1; s < max_shift; ++s) {
        size_t matches = 0;
        for (size_t i = 0; i < clean.size() - s; ++i) {
            if (clean[i] == clean[i + s]) matches++;
        }
        shifts[s] = static_cast<double>(matches) / static_cast<double>(clean.size() - s);
    }
    return shifts;
}

double ProcessedText::periodic_ioc(size_t period) const
{
    std::vector<uint8_t> clean;
    for (auto idx : m_indices) if (idx < 29) clean.push_back(idx);
    if (period < 1 || period >= clean.size()) return 0.0;

    double avg_ioc = 0.0;
    for (size_t start = 0; start < period; ++start) {
        std::vector<uint8_t> column;
        for (size_t i = start; i < clean.size(); i += period) {
            column.push_back(clean[i]);
        }
        
        if (column.size() <= 1) continue;
        
        std::array<size_t, 29> freqs{};
        for (auto idx : column) if (idx < 29) freqs[idx]++;
        
        double sum = 0.0;
        for (size_t f : freqs) { double df = static_cast<double>(f); sum += df * (df - 1.0); }
        
        double N = static_cast<double>(column.size());
        avg_ioc += sum / (N * (N - 1.0));
    }

    return avg_ioc / static_cast<double>(period);
}

std::map<std::vector<uint8_t>, std::vector<size_t>> ProcessedText::kasiski_examination(size_t seq_len) const
{
    std::map<std::vector<uint8_t>, std::vector<size_t>> repeats;
    std::vector<uint8_t> clean;
    for (auto idx : m_indices) if (idx < 29) clean.push_back(idx);
    if (seq_len < 2 || seq_len > clean.size()) return repeats;

    for (size_t i = 0; i <= clean.size() - seq_len; ++i) {
        std::vector<uint8_t> seq(clean.begin() + i, clean.begin() + i + seq_len);
        repeats[seq].push_back(i);
    }

    // Mantém apenas sequências que se repetem pelo menos uma vez
    for (auto it = repeats.begin(); it != repeats.end(); ) {
        if (it->second.size() < 2) it = repeats.erase(it);
        else ++it;
    }
    return repeats;
}

std::array<double, 29> ProcessedText::runic_distribution() const
{
    std::array<double, 29> dist{};
    size_t count = 0;
    for (auto idx : m_indices) if (idx < 29) { dist[idx]++; count++; }
    if (count == 0) return dist;
    double N = static_cast<double>(count);
    for (auto& f : dist) f /= N;
    return dist;
}

std::vector<double> ProcessedText::bigram_distribution() const
{
    std::vector<double> dist(841, 0.0);
    std::vector<uint8_t> clean;
    for (auto idx : m_indices) if (idx < 29) clean.push_back(idx);
    if (clean.size() < 2) return dist;

    for (size_t i = 0; i < clean.size() - 1; ++i) {
        dist[clean[i] * 29 + clean[i+1]]++;
    }
    
    double N = static_cast<double>(clean.size() - 1);
    for (auto& f : dist) f /= N;
    return dist;
}

std::vector<double> ProcessedText::transition_matrix() const {
    std::vector<double> matrix(841, 0.0); // 29 * 29
    std::vector<uint8_t> clean;
    for (auto idx : m_indices) if (idx < 29) clean.push_back(idx);
    
    if (clean.size() < 2) return matrix;

    std::vector<size_t> row_totals(29, 0);
    for (size_t i = 0; i < clean.size() - 1; ++i) {
        matrix[clean[i] * 29 + clean[i+1]]++;
        row_totals[clean[i]]++;
    }

    for (size_t r = 0; r < 29; ++r) {
        for (size_t c = 0; c < 29; ++c) {
            if (row_totals[r] > 0) matrix[r * 29 + c] /= static_cast<double>(row_totals[r]);
        }
    }
    return matrix;
}

std::vector<double> ProcessedText::trigram_distribution() const
{
    std::vector<double> dist(24389, 0.0); // 29^3
    std::vector<uint8_t> clean;
    for (auto idx : m_indices) if (idx < 29) clean.push_back(idx);
    if (clean.size() < 3) return dist;

    for (size_t i = 0; i < clean.size() - 2; ++i) {
        dist[clean[i] * 841 + clean[i+1] * 29 + clean[i+2]]++;
    }
    
    double N = static_cast<double>(clean.size() - 2);
    for (auto& f : dist) f /= N;
    return dist;
}

std::array<double, 26> ProcessedText::latin_distribution() const
{
    std::array<double, 26> dist{};
    size_t total = 0;
    for (auto idx : m_indices) {
        if (idx >= RUNE_TABLE.size()) continue;
        for (char c : RUNE_TABLE[idx].latin) {
            if (c >= 'A' && c <= 'Z') {
                dist[c - 'A']++;
                total++;
            }
        }
    }
    if (total > 0) {
        for (auto& f : dist) f /= static_cast<double>(total);
    }
    return dist;
}

std::optional<size_t> find_index_by_rune(std::string_view rune) {
    return RuneDB::instance().get_index_rune(rune);
}

std::optional<size_t> find_index_by_latin(std::string_view latin) {
    return RuneDB::instance().get_index_latin(latin);
}


namespace unsafe
{
    std::string_view to_latin(std::string_view rune)
    {
        // Em produção, se for 'unsafe', poderíamos acessar o mapa sem checar find()
        // mas para manter a consistência com o Pimpl:
        return RuneDB::instance().rune_to_latin(rune).value();
    }

    std::string_view to_rune(std::string_view latin)
    {
        return RuneDB::instance().latin_to_rune(latin).value();
    }

    uint16_t to_prime(std::string_view rune)
    {
        return RuneDB::instance().rune_to_prime(rune).value();
    }
} // namespace unsafe

} // namespace core