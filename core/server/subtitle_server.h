#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace overlay_trans {

struct Subtitle {
    std::string source;       // 인식된 원문 (UTF-8)
    std::string translation;  // 번역문 (UTF-8)
    // 이 자막이 대신할 이전 자막의 번호. 말이 이어져서 앞의 자막과 합쳐 다시 번역한 경우에 넣는다. 0이면 새 자막이다.
    int64_t replaces = 0;
};

// 확장 프로그램이 방송 페이지에서 읽어 보내 주는 정보.
struct StreamContext {
    std::string channel;
    std::string title;
    std::string category;
};

// 채팅 메시지 하나.
struct ChatMessage {
    std::string name;
    std::string text;
};

struct ServerConfig {
    int port = 47815;
    // 요청마다 "Authorization: Bearer <token>"으로 확인하는 페어링 키.
    std::string token;
    // 방송 정보나 새 채팅이 도착하면 서버 스레드에서 호출된다. 비워 두면 받은 내용을 버린다.
    std::function<void(StreamContext)> on_context;
    std::function<void(std::vector<ChatMessage>)> on_chat;
};

// 브라우저 확장 프로그램과 자막, 방송 정보를 주고받는 로컬 HTTP 서버.
// 이 PC(127.0.0.1)에서 온 접속만 받고, 페어링 키가 맞는 요청에만 응답한다.
//
//   GET /v1/ping                 연결과 페어링 키 확인
//   GET /v1/subtitles?after=<n>  n번 이후의 자막. 없으면 새 자막이 나올 때까지 잠시 기다린다.
//                                after를 생략하면 현재 마지막 번호만 알려 준다.
//                                자막의 replaces가 0이 아니면, 화면에 있는 그 번호의 자막을 이 자막으로 바꿔야 한다.
//   POST /v1/context             방송 정보(JSON: channel, title, category) 전달
//   POST /v1/chat                새로 올라온 채팅(JSON: messages[{name, text}]) 전달
class SubtitleServer {
public:
    SubtitleServer();
    ~SubtitleServer();

    SubtitleServer(const SubtitleServer&) = delete;
    SubtitleServer& operator=(const SubtitleServer&) = delete;

    bool start(const ServerConfig& config);
    void stop();

    // 자막을 내보내고 그 자막에 매겨진 번호를 반환한다. 어느 스레드에서 호출해도 된다.
    int64_t publish(Subtitle subtitle);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
