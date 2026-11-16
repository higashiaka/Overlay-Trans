#define NOMINMAX
#include <windows.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int wmain(int argc, wchar_t** argv) {
    const auto utf8 = [](const std::wstring& text) {
        const auto value = std::filesystem::path(text).u8string();
        return std::string(value.begin(), value.end());
    };
    const auto env = [](const wchar_t* name) {
        wchar_t value[32768]{};
        GetEnvironmentVariableW(name, value, 32768);
        return std::wstring(value);
    };
    const bool claude = std::filesystem::path(argv[0]).stem() == L"claude";
    const auto scenario_path = std::filesystem::path(env(L"OVERLAY_CLI_TEST_LOG") + L".scenario");
    const auto read_scenario = [&] {
        std::ifstream file(scenario_path);
        std::string value;
        if (file >> value) return value;
        return utf8(env(L"OVERLAY_CLI_TEST_SCENARIO"));
    };
    const auto scenario = read_scenario();
    if (argc > 1 && (std::wstring(argv[1]) == L"login" || std::wstring(argv[1]) == L"auth")) {
        if (claude) std::printf("{\"loggedIn\":%s}\n", scenario == "logged_out" ? "false" : "true");
        return scenario == "logged_out" ? 1 : 0;
    }
    nlohmann::json arguments = nlohmann::json::array();
    for (int i = 1; i < argc; ++i) arguments.push_back(utf8(argv[i]));
    const auto log_request = [&](const std::string& input, const std::string& model, int thread_count) {
        std::ofstream log(std::filesystem::path(env(L"OVERLAY_CLI_TEST_LOG")), std::ios::binary);
        log << nlohmann::json{{"arguments", arguments}, {"input", input}, {"model", model},
            {"pid", GetCurrentProcessId()}, {"threads", thread_count}, {"cwd", utf8(std::filesystem::current_path().wstring())}}.dump();
    };
    if (!claude && std::find(arguments.begin(), arguments.end(), "app-server") != arguments.end()) {
        const auto emit = [](const nlohmann::json& event) { std::cout << event.dump() << std::endl; };
        int thread_count = 0;
        int turn_count = 0;
        std::string model;
        std::string thread_id;
        std::string context;
        std::string line;
        while (std::getline(std::cin, line)) {
            const auto message = nlohmann::json::parse(line);
            const std::string method = message.value("method", "");
            if (method.empty() || !message.contains("id")) continue;
            const auto id = message["id"];
            if (method == "thread/start") {
                thread_id = "thread_" + std::to_string(++thread_count);
                model = message["params"].value("model", "");
                context.clear();
                emit({{"id", id}, {"result", {{"thread", {{"id", thread_id}}}}}});
            } else if (method == "turn/start") {
                const auto scenario_now = read_scenario();
                if (scenario_now == "process_error") return 1;
                if (scenario_now == "invalid_json") { std::cout << "invalid output" << std::endl; return 0; }
                context += message["params"]["input"][0]["text"].get<std::string>();
                log_request(context, model, thread_count);
                const auto turn_id = "turn_" + std::to_string(++turn_count);
                emit({{"id", id}, {"result", {{"turn", {{"id", turn_id}}}}}});
                emit({{"method", "item/agentMessage/delta"}, {"params", {{"threadId", "unrelated"}, {"turnId", turn_id},
                    {"itemId", "wrong"}, {"delta", "다른 자막"}}}});
                for (const auto* delta : {"테스트 ", "번역"}) {
                    emit({{"method", "item/agentMessage/delta"}, {"params", {{"threadId", thread_id}, {"turnId", turn_id},
                        {"itemId", "answer"}, {"delta", delta}}}});
                }
                emit({{"method", "item/completed"}, {"params", {{"threadId", thread_id}, {"turnId", turn_id},
                    {"item", {{"type", "agentMessage"}, {"id", "answer"}, {"text", "테스트 번역"}}}}}});
                emit({{"method", "turn/completed"}, {"params", {{"threadId", thread_id},
                    {"turn", {{"id", turn_id}, {"status", "completed"}, {"items", nlohmann::json::array()}}}}}});
            } else {
                emit({{"id", id}, {"result", nlohmann::json::object()}});
            }
        }
        return 0;
    }
    const std::string input{std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()};
    std::ofstream log(std::filesystem::path(env(L"OVERLAY_CLI_TEST_LOG")), std::ios::binary);
    log << nlohmann::json{{"arguments", arguments}, {"input", input}, {"cwd", utf8(std::filesystem::current_path().wstring())}}.dump();
    log.close();
    if (scenario == "process_error") return 1;
    if (scenario == "invalid_json") { std::printf("invalid output\n"); return 0; }
    if (claude) {
        std::printf("%s\n", nlohmann::json{{"is_error", scenario == "model_error"}, {"result", "테스트 번역"}}.dump().c_str());
    } else {
        std::printf("%s\n", nlohmann::json{{"type", "item.completed"}, {"item", {{"type", "reasoning"}, {"text", "숨겨야 할 추론"}}}}.dump().c_str());
        std::printf("%s\n", nlohmann::json{{"type", "item.completed"}, {"item", {{"type", "agent_message"}, {"text", "테스트 번역"}}}}.dump().c_str());
        std::printf("{\"type\":\"turn.completed\"}\n");
    }
    return 0;
}
