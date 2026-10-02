#include "subtitle_server.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace overlay_trans {

namespace {

constexpr const char* LOOPBACK_ADDRESS = "127.0.0.1";
constexpr const char* JSON_TYPE = "application/json; charset=utf-8";
constexpr const char* EXTENSION_ORIGIN_PREFIX = "chrome-extension://";
// 새 자막이 없을 때 응답을 붙잡아 두는 최대 시간. 확장 프로그램은 응답을 받으면 바로 다시 요청한다.
constexpr auto LONG_POLL_TIMEOUT = std::chrono::seconds(20);
// 늦게 접속한 쪽에 돌려줄 수 있도록 보관하는 자막 수.
constexpr size_t MAX_STORED_SUBTITLES = 50;
// 방송 정보로 받는 값의 상한. 넘는 부분은 버린다.
constexpr size_t MAX_CONTEXT_TEXT_BYTES = 600;
constexpr size_t MAX_NAME_BYTES = 120;
constexpr size_t MAX_CHAT_MESSAGES = 50;
constexpr size_t MAX_CHAT_TEXT_BYTES = 300;

struct StoredSubtitle {
    int64_t sequence;
    Subtitle subtitle;
};

}  // namespace

struct SubtitleServer::Impl {
    httplib::Server server;
    std::thread thread;
    ServerConfig config;

    std::mutex mutex;
    std::condition_variable changed;
    std::deque<StoredSubtitle> subtitles;
    int64_t last_sequence = 0;
    bool stopping = false;

    bool authorize(const httplib::Request& request, httplib::Response& response) const;
    void handle_subtitles(const httplib::Request& request, httplib::Response& response);
    void handle_context(const httplib::Request& request, httplib::Response& response) const;
    void handle_chat(const httplib::Request& request, httplib::Response& response) const;
};

namespace {

// JSON에서 문자열 값을 꺼낸다. 없거나 문자열이 아니면 빈 문자열, 너무 길면 잘라서 반환한다.
std::string read_text(const nlohmann::json& object, const char* key, size_t max_bytes) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string()) {
        return {};
    }
    std::string text = found->get<std::string>();
    if (text.size() > max_bytes) {
        text.resize(max_bytes);
        // UTF-8 글자 중간에서 잘렸으면 그 글자를 통째로 버린다.
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) {
            text.pop_back();
        }
        if (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0xC0) {
            text.pop_back();
        }
    }
    return text;
}

}  // namespace

bool SubtitleServer::Impl::authorize(const httplib::Request& request, httplib::Response& response) const {
    // 일반 웹페이지가 보낸 요청은 거부한다. 확장 프로그램이나 브라우저 밖에서 온 요청만 받는다.
    const std::string origin = request.get_header_value("Origin");
    if (!origin.empty()) {
        if (origin.rfind(EXTENSION_ORIGIN_PREFIX, 0) != 0) {
            response.status = 403;
            return false;
        }
        response.set_header("Access-Control-Allow-Origin", origin);
        response.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
    }

    // 브라우저가 본 요청 전에 보내는 확인 요청에는 페어링 키가 실려 있지 않다.
    if (request.method == "OPTIONS") {
        response.status = 204;
        return false;
    }

    if (request.get_header_value("Authorization") != "Bearer " + config.token) {
        response.status = 401;
        return false;
    }
    return true;
}

void SubtitleServer::Impl::handle_subtitles(const httplib::Request& request, httplib::Response& response) {
    nlohmann::json body;
    body["subtitles"] = nlohmann::json::array();

    std::unique_lock lock(mutex);
    if (request.has_param("after")) {
        int64_t after = 0;
        try {
            after = std::stoll(request.get_param_value("after"));
        } catch (const std::exception&) {
            response.status = 400;
            return;
        }
        // 앱이 다시 시작되면 번호가 처음부터 매겨지므로, 더 큰 번호를 들고 온 쪽은 처음부터 다시 받게 한다.
        if (after > last_sequence) {
            after = 0;
        }

        changed.wait_for(lock, LONG_POLL_TIMEOUT, [&] { return stopping || last_sequence > after; });
        for (const StoredSubtitle& stored : subtitles) {
            if (stored.sequence > after) {
                body["subtitles"].push_back({
                    {"sequence", stored.sequence},
                    {"source", stored.subtitle.source},
                    {"translation", stored.subtitle.translation},
                });
            }
        }
    }
    body["latest"] = last_sequence;
    lock.unlock();

    // 인식 결과에 깨진 글자가 섞여 있어도 응답이 실패하지 않게 한다.
    response.set_content(body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), JSON_TYPE);
}

void SubtitleServer::Impl::handle_context(const httplib::Request& request, httplib::Response& response) const {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (!body.is_object()) {
        response.status = 400;
        return;
    }

    StreamContext context;
    context.channel = read_text(body, "channel", MAX_CONTEXT_TEXT_BYTES);
    context.title = read_text(body, "title", MAX_CONTEXT_TEXT_BYTES);
    context.category = read_text(body, "category", MAX_CONTEXT_TEXT_BYTES);

    if (config.on_context) {
        config.on_context(std::move(context));
    }
    response.set_content(R"({"ok":true})", JSON_TYPE);
}

void SubtitleServer::Impl::handle_chat(const httplib::Request& request, httplib::Response& response) const {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (!body.is_object()) {
        response.status = 400;
        return;
    }

    std::vector<ChatMessage> messages;
    if (const auto list = body.find("messages"); list != body.end() && list->is_array()) {
        for (const nlohmann::json& item : *list) {
            if (messages.size() >= MAX_CHAT_MESSAGES) {
                break;
            }
            if (!item.is_object()) {
                continue;
            }
            ChatMessage message{read_text(item, "name", MAX_NAME_BYTES), read_text(item, "text", MAX_CHAT_TEXT_BYTES)};
            if (!message.text.empty()) {
                messages.push_back(std::move(message));
            }
        }
    }

    if (config.on_chat && !messages.empty()) {
        config.on_chat(std::move(messages));
    }
    response.set_content(R"({"ok":true})", JSON_TYPE);
}

SubtitleServer::SubtitleServer() : impl_(std::make_unique<Impl>()) {}

SubtitleServer::~SubtitleServer() {
    stop();
}

bool SubtitleServer::start(const ServerConfig& config) {
    impl_->config = config;

    impl_->server.set_pre_routing_handler([this](const httplib::Request& request, httplib::Response& response) {
        return impl_->authorize(request, response) ? httplib::Server::HandlerResponse::Unhandled
                                                   : httplib::Server::HandlerResponse::Handled;
    });
    impl_->server.Get("/v1/ping", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(R"({"app":"overlay-trans","version":1})", JSON_TYPE);
    });
    impl_->server.Get("/v1/subtitles", [this](const httplib::Request& request, httplib::Response& response) {
        impl_->handle_subtitles(request, response);
    });
    impl_->server.Post("/v1/context", [this](const httplib::Request& request, httplib::Response& response) {
        impl_->handle_context(request, response);
    });
    impl_->server.Post("/v1/chat", [this](const httplib::Request& request, httplib::Response& response) {
        impl_->handle_chat(request, response);
    });

#ifdef _WIN32
    // 라이브러리 기본 설정(SO_REUSEADDR)은 Windows에서 다른 프로그램이 같은 포트를 함께 여는 것을 허용한다.
    // 포트를 혼자 쓰도록 바꿔서, 이미 쓰이고 있으면 시작에 실패하게 한다.
    impl_->server.set_socket_options([](socket_t socket) {
        const int exclusive = 1;
        setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
                   sizeof(exclusive));
    });
#endif

    if (!impl_->server.bind_to_port(LOOPBACK_ADDRESS, config.port)) {
        return false;
    }
    impl_->thread = std::thread([this] { impl_->server.listen_after_bind(); });
    return true;
}

void SubtitleServer::stop() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->changed.notify_all();

    impl_->server.stop();
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

void SubtitleServer::publish(Subtitle subtitle) {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->subtitles.push_back({++impl_->last_sequence, std::move(subtitle)});
        if (impl_->subtitles.size() > MAX_STORED_SUBTITLES) {
            impl_->subtitles.pop_front();
        }
    }
    impl_->changed.notify_all();
}

}  // namespace overlay_trans
