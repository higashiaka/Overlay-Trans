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
};

bool SubtitleServer::Impl::authorize(const httplib::Request& request, httplib::Response& response) const {
    // 일반 웹페이지가 보낸 요청은 거부한다. 확장 프로그램이나 브라우저 밖에서 온 요청만 받는다.
    const std::string origin = request.get_header_value("Origin");
    if (!origin.empty()) {
        if (origin.rfind(EXTENSION_ORIGIN_PREFIX, 0) != 0) {
            response.status = 403;
            return false;
        }
        response.set_header("Access-Control-Allow-Origin", origin);
        response.set_header("Access-Control-Allow-Headers", "Authorization");
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
