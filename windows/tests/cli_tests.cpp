#include "inference/cli_translator.h"
#include "inference/translation_worker.h"
#include "inference/deepl_translator.h"
#include "inference/drafted_translator.h"
#include "net/winhttp_client.h"
#include "translation_settings.h"

#define NOMINMAX
#include <httplib.h>
#include <windows.h>
#include <dpapi.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return nlohmann::json::parse(file);
}

class RecordingDeepL final : public overlay_trans::Translator {
public:
    overlay_trans::DeepLTranslator translator;
    std::string last_draft;
    long long last_ms = 0;
    RecordingDeepL(const overlay_trans::TranslatorConfig& config, const overlay_trans::DeepLTranslatorConfig& deepl)
        : translator(std::make_unique<overlay_trans::WinHttpClient>(), config, deepl) {}
    void set_stream_info(const std::string& info) override { translator.set_stream_info(info); }
    std::string translate(std::string_view text, std::span<const std::string> chat, bool replace,
        const PartialCallback& on_partial) override {
        const auto started = std::chrono::steady_clock::now();
        last_draft = translator.translate(text, chat, replace, on_partial);
        last_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        return last_draft;
    }
};

class StubTranslator final : public overlay_trans::Translator {
public:
    std::string output;
    std::string received_draft;
    explicit StubTranslator(std::string value) : output(std::move(value)) {}
    void set_stream_info(const std::string&) override {}
    std::string translate(std::string_view, std::span<const std::string>, bool, const PartialCallback& partial) override {
        if (partial && !output.empty()) partial(output);
        return output;
    }
    std::string translate_with_draft(std::string_view text, std::span<const std::string> chat, bool replace,
        const PartialCallback& partial, const std::string& draft) override {
        received_draft = draft;
        return translate(text, chat, replace, partial);
    }
};
}

int wmain(int argc, wchar_t** argv) {
    using namespace overlay_trans;
    try {
        if (argc >= 2 && std::wstring(argv[1]) == L"--compare-deepl-codex") {
            const auto settings = load_translation_settings();
            check(!settings.deepl_key.empty(), "Save a DeepL API key in app settings before comparing");
            CliAccounts accounts;
            check(accounts.status("codex").logged_in, "Codex account unavailable");
            std::string model = settings.codex_model;
            if (argc >= 3) {
                const auto value = std::filesystem::path(argv[2]).u8string();
                model.assign(value.begin(), value.end());
            }
            TranslatorConfig config;
            CliTranslator codex("codex", model, config);
            config.reference_drafts = true;
            auto draft = std::make_unique<RecordingDeepL>(config, DeepLTranslatorConfig{settings.deepl_key, "JA"});
            auto* recording = draft.get();
            DraftedTranslator hybrid(std::move(draft), std::make_unique<CliTranslator>("codex", model, config), "Korean", true);
            const std::string stream = "Japanese live stream: casual gaming conversation.";
            codex.set_stream_info(stream);
            hybrid.set_stream_info(stream);
            const std::vector<std::string> samples{
                "気を付けた方がいいと思うけどね。あのゲーム、職場でも流行ってるんだよ。めっちゃ楽しい。",
                "普通にいいのでは？",
                "今日はね、まず",
                "下手くそだけど楽しいんだよね。ずっと味方を撃っちゃってるけど。",
                "ミントさん、スパイク持ってきて。あと二人残ってるから気を付けて。"};
            nlohmann::json rows = nlohmann::json::array();
            const auto measure = [](Translator& translator, const std::string& source) {
                const auto started = std::chrono::steady_clock::now();
                long long first = -1;
                const auto translated = translator.translate(source, {}, false, [&](std::string_view) {
                    if (first < 0) first = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
                });
                const auto complete = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
                check(!translated.empty(), "Translation comparison failed");
                return nlohmann::json{{"translation", translated}, {"first_ms", first}, {"complete_ms", complete}};
            };
            for (size_t i = 0; i < samples.size(); ++i) {
                nlohmann::json pure;
                nlohmann::json refined;
                if (i % 2 == 0) { pure = measure(codex, samples[i]); refined = measure(hybrid, samples[i]); }
                else { refined = measure(hybrid, samples[i]); pure = measure(codex, samples[i]); }
                check(!recording->last_draft.empty(), "DeepL draft failed; cannot compare hybrid quality");
                rows.push_back({{"source", samples[i]}, {"codex", pure}, {"hybrid", refined},
                    {"deepl_draft", recording->last_draft}, {"deepl_ms", recording->last_ms}, {"cold_start", i == 0}});
                std::printf("Sample %zu | Codex first %lld / complete %lld ms | DeepL+Codex first %lld / complete %lld ms\n",
                    i + 1, pure["first_ms"].get<long long>(), pure["complete_ms"].get<long long>(),
                    refined["first_ms"].get<long long>(), refined["complete_ms"].get<long long>());
                std::fflush(stdout);
            }
            const auto output = std::filesystem::path("build/cli-comparison/deepl-codex.json");
            std::filesystem::create_directories(output.parent_path());
            std::ofstream file(output, std::ios::binary);
            file << nlohmann::json{{"model", model}, {"target_language", "Korean"}, {"samples", rows}}.dump(2);
            check(static_cast<bool>(file), "Could not save comparison report");
            std::printf("Report: build/cli-comparison/deepl-codex.json\n");
            return 0;
        }
        if (argc >= 2 && std::wstring(argv[1]) == L"--smoke-codex") {
            CliAccounts accounts;
            check(accounts.status("codex").logged_in, "Codex account unavailable");
            std::string model;
            if (argc >= 3) {
                const auto name = std::filesystem::path(argv[2]).u8string();
                model.assign(name.begin(), name.end());
            }
            CliTranslator translator("codex", model, {});
            const int count = argc >= 4 ? static_cast<int>(std::clamp(std::wcstol(argv[3], nullptr, 10), 1L, 8L)) : 1;
            for (int i = 0; i < count; ++i) {
                const auto started = std::chrono::steady_clock::now();
                long long first_ms = -1;
                const auto result = translator.translate("気を付けた方がいいと思うけどね。あのゲーム、職場でも流行ってるんだよ。めっちゃ楽しい。", {}, false,
                    [&](std::string_view) {
                        if (first_ms < 0) first_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
                    });
                check(!result.empty(), "Real Codex translation failed");
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
                std::printf("Codex model: %s | request %d | first: %lld ms | complete: %lld ms\n%s\n", model.empty() ? "default" : model.c_str(),
                            i + 1, first_ms, static_cast<long long>(elapsed), result.c_str());
                std::fflush(stdout);
            }
            return 0;
        }
        check(argc == 2, "Expected test helper path");
        {
            auto refiner = std::make_unique<StubTranslator>("자연스럽게 다듬은 번역");
            auto* recorded = refiner.get();
            DraftedTranslator hybrid(std::make_unique<StubTranslator>("초안"), std::move(refiner), "Korean", true);
            std::vector<std::string> previews;
            check(hybrid.translate("原文", {}, false, [&](auto text) { previews.emplace_back(text); }) == "자연스럽게 다듬은 번역",
                  "CLI draft refinement failed");
            check(recorded->received_draft == "초안", "Refiner did not receive draft");
            check(previews == std::vector<std::string>{"초안"}, "Draft preview flickered during refinement");
            DraftedTranslator fallback(std::make_unique<StubTranslator>("응"), std::make_unique<StubTranslator>(""), "Korean", true);
            check(fallback.translate("はい", {}, false, {}) == "응", "Short draft was lost after refiner failure");
            DraftedTranslator no_draft(std::make_unique<StubTranslator>(""), std::make_unique<StubTranslator>("직접 번역"), "Korean", true);
            check(no_draft.translate("原文", {}, false, {}) == "직접 번역", "DeepL failure blocked direct translation");
        }
        for (bool batching : {false, true}) {
            std::mutex mutex;
            std::condition_variable changed;
            bool first_started = false;
            bool release_first = false;
            std::vector<std::string> texts;
            std::vector<size_t> batches;
            TranslationWorker worker([&](const RecognizedSpeech& speech) {
                std::unique_lock lock(mutex);
                texts.push_back(speech.text);
                batches.push_back(speech.batched_segments);
                if (!first_started) {
                    first_started = true;
                    changed.notify_all();
                    changed.wait(lock, [&] { return release_first; });
                }
                check(!speech.samples.empty() && speech.samples.front() == 1.0f, "Queued audio sample ownership lost");
            });
            worker.set_batching(batching);
            std::vector<float> samples{1.0f, 2.0f};
            worker.submit({.samples = samples, .text = "first"});
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return first_started; });
            }
            // 앞 번역을 붙잡아 둬도 인식 결과 제출은 완료되어야 한다.
            worker.submit({.samples = samples, .text = "second"});
            worker.submit({.samples = samples, .text = "third"});
            worker.submit({.samples = samples, .text = "fourth"});
            samples.assign(2, 9.0f);
            {
                std::lock_guard lock(mutex);
                release_first = true;
            }
            changed.notify_all();
            worker.wait_until_idle();
            if (batching) {
                check(texts == std::vector<std::string>{"first", "second\nthird\nfourth"}, "Pending subtitles were lost or reordered");
                check(batches == std::vector<size_t>{1, 3}, "CLI batch did not reduce request count");
            } else {
                check(texts == std::vector<std::string>{"first", "second", "third", "fourth"}, "Non-CLI subtitles were batched");
            }
        }
        wchar_t temporary[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temporary);
        const auto root = std::filesystem::path(temporary) / (L"overlay cli 한글 tests-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() { std::error_code error; std::filesystem::remove_all(root, error); }
        } cleanup{root};
        std::filesystem::copy_file(argv[1], root / "codex.exe", std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(argv[1], root / "claude.exe", std::filesystem::copy_options::overwrite_existing);
        SetEnvironmentVariableW(L"PATH", root.c_str());
        SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
        const auto session_path = root / L"OverlayTrans/ChatGPT/session.bin";
        std::filesystem::create_directories(session_path.parent_path());
        auto save_test_session = [&] {
            auto record = nlohmann::json{{"scope", "chatgpt.tokens.use.direct"},
                {"access_token", "fixture-access"}, {"refresh_token", "fixture-refresh"},
                {"client_id", "oaiapp_fixture"}, {"expires_at", 4102444800LL}}.dump();
            DATA_BLOB input{static_cast<DWORD>(record.size()), reinterpret_cast<BYTE*>(record.data())}, output{};
            check(CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output),
                  "Fixture credentials could not be encrypted");
            std::ofstream file(session_path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(output.pbData), output.cbData);
            LocalFree(output.pbData);
            check(static_cast<bool>(file), "Fixture credentials could not be saved");
        };
        save_test_session();
        const auto log_path = root / L"request.json";
        SetEnvironmentVariableW(L"OVERLAY_CLI_TEST_LOG", log_path.c_str());
        const auto set_scenario = [&](const wchar_t* scenario) {
            SetEnvironmentVariableW(L"OVERLAY_CLI_TEST_SCENARIO", scenario);
            const std::wstring wide(scenario);
            std::ofstream file(std::filesystem::path(log_path.wstring() + L".scenario"));
            file << std::string(wide.begin(), wide.end());
        };
        set_scenario(L"ok");
        CliAccounts accounts;
        check(!accounts.status("invalid").installed, "Invalid provider accepted");
        check(!accounts.connect("invalid"), "Invalid login provider accepted");
        for (const std::string provider : {"codex", "claude"}) {
            auto status = accounts.status(provider);
            check(status.installed && status.logged_in, "Signed-in status not detected");
            set_scenario(L"logged_out");
            if (provider == "codex") DeleteFileW(session_path.c_str());
            check(!accounts.status(provider).logged_in, "Signed-out status not detected");
            if (provider == "codex") save_test_session();
            set_scenario(L"ok");
            const std::string model = "model with spaces \"quotes\" & $(literal) \\";
            CliTranslator translator(provider, model, {});
            translator.set_stream_info("방송 테스트");
            std::string partial;
            size_t partial_count = 0;
            const std::string text = "こんにちは\n & echo must-not-run | $(test) \"인용\"";
            check(translator.translate(text, {}, false, [&](auto value) { partial = value; ++partial_count; }) == "테스트 번역", "Translation failed");
            check(partial == "테스트 번역", "Translation callback mismatch");
            auto request = read_json(log_path);
            const auto arguments = request["arguments"].get<std::vector<std::string>>();
            const auto model_flag = std::find(arguments.begin(), arguments.end(), "--model");
            if (provider == "claude") {
                check(model_flag != arguments.end() && std::next(model_flag) != arguments.end() && *std::next(model_flag) == model,
                      "Windows argument quoting failed");
            } else {
                check(request["model"] == model, "Model name changed in JSON-RPC");
                check(partial_count >= 2, "Codex deltas did not stream");
            }
            check(request["input"].get<std::string>().find(text) != std::string::npos, "UTF-8 stdin changed");
            const auto cwd = request["cwd"].get<std::string>();
            const auto workspace = std::filesystem::path(std::u8string(cwd.begin(), cwd.end()));
            check(std::filesystem::exists(workspace) == (provider == "codex"), "CLI workspace lifecycle incorrect");
            if (provider == "claude") {
                const auto tools = std::find(arguments.begin(), arguments.end(), "--tools");
                check(tools != arguments.end() && std::next(tools) != arguments.end() && std::next(tools)->empty(), "Claude tools not disabled");
                set_scenario(L"model_error");
                check(translator.translate("실패", {}, false, {}).empty(), "Claude model error shown as translation");
            } else {
                check(std::find(arguments.begin(), arguments.end(), "features.shell_tool=false") != arguments.end(), "Codex shell not disabled");
            }
            set_scenario(L"process_error");
            check(translator.translate("실패", {}, true, {}).empty(), "Process failure shown as translation");
            set_scenario(L"invalid_json");
            check(translator.translate("실패", {}, false, {}).empty(), "Invalid JSON shown as translation");
            set_scenario(L"ok");
            check(!translator.translate("이어진 문장", {}, true, {}).empty(), "Replacement failed");
            check(read_json(log_path)["input"].get<std::string>().find("테스트 번역") == std::string::npos,
                  "Replaced subtitle remained in context");
            const auto before_followup = read_json(log_path);
            check(!translator.translate("다음 문장", {}, false, {}).empty(), "Follow-up failed");
            check(read_json(log_path)["input"].get<std::string>().find("이어진 문장") != std::string::npos,
                  "Recent context missing");
            if (provider == "codex") {
                const auto after_followup = read_json(log_path);
                check(before_followup["pid"] == after_followup["pid"] && after_followup["threads"].get<int>() == before_followup["threads"].get<int>() + 1,
                      "Codex should reuse its process and start a fresh thread for each subtitle");
                check(!std::filesystem::exists(workspace), "Failed Codex session workspace was not cleaned up");
                check(!translator.translate_with_draft("원문", {}, false, {}, "기계 번역 초안").empty(), "Codex draft request failed");
                check(read_json(log_path)["input"].get<std::string>().find("[Draft]\n기계 번역 초안") != std::string::npos,
                      "Codex did not receive the current DeepL draft");
            }
        }

        {
            TranslatorConfig config;
            config.context_lines = 1;
            CliTranslator translator("codex", "test-model", config);
            check(!translator.translate("first", {}, false, {}).empty(), "Codex initial session failed");
            const auto first = read_json(log_path);
            check(!translator.translate("second", {}, false, {}).empty(), "Codex context rollover failed");
            const auto second = read_json(log_path);
            check(first["pid"] == second["pid"] && second["threads"].get<int>() == first["threads"].get<int>() + 1,
                  "Context rollover restarted process or did not reset thread");
            check(second["input"].get<std::string>().find("first") != std::string::npos, "Previous subtitle was lost");
            translator.set_stream_info("changed stream");
            check(!translator.translate("third", {}, false, {}).empty(), "Codex stream-info reset failed");
            const auto third = read_json(log_path);
            check(second["pid"] == third["pid"] && third["threads"].get<int>() == second["threads"].get<int>() + 1,
                  "Stream-info reset restarted process or reused stale instructions");
            check(third["input"].get<std::string>().find("second") == std::string::npos, "Old stream leaked into history");
            check(!translator.translate(std::string(2200, 'x'), {}, false, {}).empty(), "Long subtitle failed");
            check(!translator.translate("short", {}, false, {}).empty(), "Short follow-up failed");
            check(read_json(log_path)["input"].get<std::string>().find(std::string(50, 'x')) == std::string::npos,
                  "Oversized previous subtitle leaked into history");
        }

        TranslationSettings saved;
        SubtitleServer server;
        const int port = 49000 + GetCurrentProcessId() % 10000;
        check(server.start({.port = port, .token = "test-token", .get_settings = [&] { return saved; },
            .save_settings = [&](const TranslationSettings& settings) { saved = settings; return true; },
            .get_cli_status = [&](const std::string& provider) { return accounts.status(provider); },
            .connect_cli = [](const std::string&) { return true; }}), "Test server failed to start");
        httplib::Client client("127.0.0.1", port);
        client.set_default_headers({{"Authorization", "Bearer test-token"}});
        auto response = client.Get("/v1/cli/codex");
        check(response && response->status == 200 && nlohmann::json::parse(response->body)["logged_in"] == true, "CLI status endpoint failed");
        response = client.Get("/v1/cli/invalid");
        check(response && response->status == 404, "Invalid provider route accepted");
        const std::string settings = R"({"source_language":"auto","target_language":"Korean","translator":"codex","codex_model":"gpt-test","claude_model":"claude-test"})";
        response = client.Post("/v1/settings", settings, "application/json");
        check(response && response->status == 200 && saved.translator == "codex" && saved.codex_model == "gpt-test" &&
              saved.claude_model == "claude-test", "CLI settings required a DeepL key or lost models");
        auto hybrid_settings = nlohmann::json::parse(settings);
        hybrid_settings["translator"] = "codex-hybrid";
        response = client.Post("/v1/settings", hybrid_settings.dump(), "application/json");
        check(response && response->status == 400, "DeepL+Codex accepted without DeepL key");
        hybrid_settings["deepl_key"] = "test-key";
        response = client.Post("/v1/settings", hybrid_settings.dump(), "application/json");
        check(response && response->status == 200 && saved.translator == "codex-hybrid",
              "DeepL+Codex mode was truncated or mapped to wrong account provider");
        DeleteFileW(session_path.c_str());
        response = client.Post("/v1/settings", settings, "application/json");
        check(response && response->status == 409, "Disconnected CLI settings accepted");
        response = client.Post("/v1/cli/claude/connect", "", "application/json");
        check(response && response->status == 200, "CLI connect endpoint failed");
        client.set_default_headers({});
        response = client.Post("/v1/cli/codex/connect", "", "application/json");
        check(response && response->status == 401, "Login endpoint bypassed pairing");
        client.set_default_headers({{"Authorization", "Bearer test-token"}, {"Origin", "https://example.com"}});
        response = client.Post("/v1/cli/codex/connect", "", "application/json");
        check(response && response->status == 403, "Web page triggered account login");
        server.stop();
        std::printf("CLI process, history, account status, and HTTP integration tests passed.\n");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
