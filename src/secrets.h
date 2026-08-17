#ifndef SECRETS_H
#define SECRETS_H

#include <vector>
#include <string_view>
#include <string>

namespace core::secrets {

    extern const std::string_view DEEP_HASH;

    extern const std::vector<std::vector<std::vector<int>>> SQUARES;

    extern const std::vector<int> MISSING_PRIMES_2013;

    extern const std::vector<std::string_view> CUNEIFORM_STREAM;

} // namespace core::secrets

#endif // SECRETS_H