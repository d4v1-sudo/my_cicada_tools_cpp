#ifndef CORE_H
#define CORE_H

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <string>
#include <memory>
#include <vector>
#include <map>

namespace core
{
    struct Page; // Forward declaration
    class ProcessedText; // Forward declaration

// ---------------------------------------------------------------------------
// Rune table
// ---------------------------------------------------------------------------

// uint16_t supports future primes without overflow (the 29th prime is 109,
// it fits in uint8_t but the margin is small; uint16_t allows more room)
struct RuneEntry
{
    std::string_view rune;
    std::string_view latin;
    uint16_t         prime;
};

inline constexpr std::array<RuneEntry, 29> RUNE_TABLE = {{
    {"ᚠ",  "F",   2},
    {"ᚢ",  "V",   3},
    {"ᚦ",  "TH",  5},
    {"ᚩ",  "O",   7},
    {"ᚱ",  "R",   11},
    {"ᚳ",  "C",   13},
    {"ᚷ",  "G",   17},
    {"ᚹ",  "W",   19},
    {"ᚻ",  "H",   23},
    {"ᚾ",  "N",   29},
    {"ᛁ",  "I",   31},
    {"ᛄ",  "J",   37},
    {"ᛇ",  "EO",  41},
    {"ᛈ",  "P",   43},
    {"ᛉ",  "X",   47},
    {"ᛋ",  "S",   53},
    {"ᛏ",  "T",   59},
    {"ᛒ",  "B",   61},
    {"ᛖ",  "E",   67},
    {"ᛗ",  "M",   71},
    {"ᛚ",  "L",   73},
    {"ᛝ",  "NG",  79},
    {"ᛟ",  "OE",  83},
    {"ᛞ",  "D",   89},
    {"ᚪ",  "A",   97},
    {"ᚫ",  "AE",  101},
    {"ᚣ",  "Y",   103},
    {"ᛡ",  "IA",  107},
    {"ᛠ",  "EA",  109},
}};

// Delimiters defined according to ENTITY_LOOKUP
inline const std::map<std::string, std::string_view> DELIMITERS = {
    {"-", "word"},
    {".", "clause"},
    {"&", "paragraph"},
    {"$", "segment"},
    {"§", "chapter"},
    {"/", "line"},
    {"%", "page"}
};

// Helper to obtain the index of a rune in RUNE_TABLE
std::optional<size_t> find_index_by_rune(std::string_view rune);
std::optional<size_t> find_index_by_latin(std::string_view latin);

// Special indices for non-rune characters in ProcessedText::m_indices
// These are > 28 (max rune index) and are used for display purposes
// and to be skipped by cryptographic transformations.
constexpr uint8_t SPECIAL_HYPHEN_IDX        = 29; // Mapped to ' ' in to_latin()
constexpr uint8_t SPECIAL_PERIOD_IDX        = 30; // Mapped to '.'
constexpr uint8_t SPECIAL_AMPERSAND_IDX     = 31; // Mapped to '&'
constexpr uint8_t SPECIAL_DOLLAR_IDX        = 32; // Mapped to '$'
constexpr uint8_t SPECIAL_SECTION_IDX       = 33; // Mapped to '§'
constexpr uint8_t SPECIAL_SLASH_IDX         = 34; // Mapped to '/'
constexpr uint8_t SPECIAL_PERCENT_IDX       = 35; // Mapped to '%'
constexpr uint8_t SPECIAL_SPACE_IDX         = 36; // Mapped to ' '
constexpr uint8_t SPECIAL_NEWLINE_IDX       = 37; // Mapped to '\n'
constexpr uint8_t SPECIAL_DIGIT_OFFSET      = 38; // '0' -> 38, '1' -> 39, ..., '9' -> 47
constexpr uint8_t SPECIAL_HEX_LETTER_OFFSET = 48; // 'a' -> 48, 'b' -> 49, ..., 'f' -> 53
constexpr uint8_t SPECIAL_UNKNOWN_CHAR_IDX  = 250; // For any other unrecognized char

// --- Mathematical functions for cryptanalysis ---
long long gcd(long long a, long long b);
int euler_totient(int n);
bool is_prime(int n);
int find_next_prime(int n);
int reverse_digits(int n);
int mobius(int n);
double kullback_leibler_divergence(const std::array<double, 29>& p, const std::array<double, 29>& q);

// Global list of primes for efficient lookup
extern std::vector<int> G_PRIMES;
void populate_primes_list(size_t limit);

// --- Known solutions and logic (implemented in solutions.cpp) ---
bool has_known_solution(size_t page_index);
std::vector<size_t> get_possible_interrupters(size_t page_index);
bool apply_known_solution(const Page& page, ProcessedText& pt);
std::string get_solution_method(size_t page_index);

struct Solution {
    std::string_view page_name;
    std::string transformer_type;
    std::string parameters;
    std::string full_decrypted_text;
};
void save_solution(const Solution& sol);

// ---------------------------------------------------------------------------
// RuneDB — encapsulates lookup maps; initialized once via init_tables()
// ---------------------------------------------------------------------------

// Class declaration — defined in core.cpp
class RuneDB
{
public:
    static RuneDB& instance();
    ~RuneDB();

    // Returns nullopt if not found
    std::optional<std::string_view> rune_to_latin(std::string_view rune)  const;
    std::optional<std::string_view> latin_to_rune(std::string_view latin) const;
    std::optional<uint16_t>         rune_to_prime(std::string_view rune)  const;
    std::optional<uint16_t>         latin_to_prime(std::string_view latin) const;

    // Lookup by index (0–28) — useful for iterating
    std::optional<const RuneEntry*> by_index(size_t idx) const noexcept;

    // Methods to get raw indices for ProcessedText
    std::optional<size_t> get_index_rune(std::string_view rune) const;
    std::optional<size_t> get_index_latin(std::string_view latin) const;

private:
    RuneDB();  // constructor builds the lookup maps
    // sem herança, sem cópia
    RuneDB(const RuneDB&)            = delete;
    RuneDB& operator=(const RuneDB&) = delete;

    // Implementação interna em core.cpp
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------------------
// Thin API — delegates to RuneDB::instance() internally
// Compatible with original project style
// ---------------------------------------------------------------------------

void init_tables();   // calls RuneDB::instance() to force construction

std::optional<std::string_view> to_latin(std::string_view rune);
std::optional<std::string_view> to_rune (std::string_view latin);
std::optional<uint16_t>         to_prime(std::string_view rune);

// Convert an ASCII/Latin string (e.g. "DIVINITY" or "CIRCUMFERENCE") to
// a UTF-8 runes string using the rune table. Returns nullopt on invalid input.
std::optional<std::string> to_runes(std::string_view latin_text);

// Convert an ASCII/Latin string (or a runes UTF-8 string) into rune indices
// (0..28). Handles common linguistic bigrams like TH, NG, AE, etc.
std::optional<std::vector<uint8_t>> to_rune_indices(std::string_view latin_or_runes);

// Patch a textual key so that all occurrences of the first rune are
// replaced by the latin representation of rune 0 and all others are left
// as their latin representation. This mirrors the user's patch_key logic
// used when working with discovered Vigenere candidate keys.
std::string patch_key(const std::string& key);

// ---------------------------------------------------------------------------
// Namespace unsafe — no bounds checks, UB if key is missing.
// Use only when you already ensured the key exists.
// ---------------------------------------------------------------------------
namespace unsafe
{
    std::string_view to_latin(std::string_view rune);
    std::string_view to_rune (std::string_view latin);
    uint16_t         to_prime(std::string_view rune);
} // namespace unsafe

} // namespace core

// Include dos novos componentes no namespace core
#include "ProcessedText.h"
#include "Transformers.h"

#endif // CORE_H