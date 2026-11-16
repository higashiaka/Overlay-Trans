#include "translation_settings.h"

#define NOMINMAX
#include <windows.h>
#include <dpapi.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <vector>

namespace overlay_trans {
namespace {

std::string hex_encode(const BYTE* data, size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        result.push_back(digits[data[i] >> 4]);
        result.push_back(digits[data[i] & 15]);
    }
    return result;
}

std::vector<BYTE> hex_decode(const std::string& text) {
    if (text.size() % 2 != 0) return {};
    std::vector<BYTE> bytes;
    for (size_t i = 0; i < text.size(); i += 2) {
        const auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        const int high = digit(text[i]);
        const int low = digit(text[i + 1]);
        if (high < 0 || low < 0) return {};
        bytes.push_back(static_cast<BYTE>(high * 16 + low));
    }
    return bytes;
}

}  // namespace

std::filesystem::path translation_settings_path() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) return {};
    const std::filesystem::path path = std::filesystem::path(folder) / "OverlayTrans" / "translation-settings.json";
    CoTaskMemFree(folder);
    return path;
}

TranslationSettings load_translation_settings() {
    TranslationSettings settings;
    std::ifstream file(translation_settings_path(), std::ios::binary);
    if (!file) return settings;
    const auto body = nlohmann::json::parse(file, nullptr, false);
    if (!body.is_object()) return settings;
    settings.source_language = body.value("source_language", "auto");
    settings.target_language = body.value("target_language", "Korean");
    settings.translator = body.value("translator", "local");
    settings.codex_model = body.value("codex_model", "");
    settings.claude_model = body.value("claude_model", "");
    auto encrypted = hex_decode(body.value("deepl_key_protected", ""));
    if (!encrypted.empty()) {
        DATA_BLOB input{static_cast<DWORD>(encrypted.size()), encrypted.data()};
        DATA_BLOB output{};
        if (CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output)) {
            settings.deepl_key.assign(reinterpret_cast<const char*>(output.pbData), output.cbData);
            SecureZeroMemory(output.pbData, output.cbData);
            LocalFree(output.pbData);
        }
    }
    settings.has_deepl_key = !settings.deepl_key.empty();
    return settings;
}

bool save_translation_settings(const TranslationSettings& settings) {
    const auto path = translation_settings_path();
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    nlohmann::json body{{"source_language", settings.source_language},
                        {"target_language", settings.target_language}, {"translator", settings.translator},
                        {"codex_model", settings.codex_model}, {"claude_model", settings.claude_model}};
    if (!settings.deepl_key.empty()) {
        DATA_BLOB input{static_cast<DWORD>(settings.deepl_key.size()),
                        reinterpret_cast<BYTE*>(const_cast<char*>(settings.deepl_key.data()))};
        DATA_BLOB output{};
        if (!CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output)) return false;
        body["deepl_key_protected"] = hex_encode(output.pbData, output.cbData);
        LocalFree(output.pbData);
    }
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << body.dump();
        if (!file) return false;
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    return !error;
}

}  // namespace overlay_trans
