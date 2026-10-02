#include "pairing_token.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <random>
#include <system_error>

namespace overlay_trans {

namespace {

constexpr size_t TOKEN_LENGTH = 16;
// 손으로 옮겨 적을 때 헷갈리는 글자(0/O, 1/I/L)를 뺀 31글자. 16글자면 약 79비트다.
constexpr char ALPHABET[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";

bool is_valid_token(const std::string& token) {
    return token.size() == TOKEN_LENGTH &&
           std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isalnum(c) != 0; });
}

std::string generate_token() {
    std::random_device random;
    std::string token;
    for (size_t i = 0; i < TOKEN_LENGTH; ++i) {
        token += ALPHABET[random() % (sizeof(ALPHABET) - 1)];
    }
    return token;
}

}  // namespace

std::string load_or_create_pairing_token(const std::filesystem::path& file) {
    std::string token;
    if (std::ifstream input(file); input >> token && is_valid_token(token)) {
        return token;
    }

    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);

    token = generate_token();
    std::ofstream output(file, std::ios::trunc);
    output << token << '\n';
    return output.good() ? token : std::string();
}

}  // namespace overlay_trans
