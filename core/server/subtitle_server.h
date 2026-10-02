#pragma once

#include <memory>
#include <string>

namespace overlay_trans {

struct Subtitle {
    std::string source;       // 인식된 원문 (UTF-8)
    std::string translation;  // 번역문 (UTF-8)
};

struct ServerConfig {
    int port = 47815;
    // 요청마다 "Authorization: Bearer <token>"으로 확인하는 페어링 키.
    std::string token;
};

// 브라우저 확장 프로그램에 자막을 전달하는 로컬 HTTP 서버.
// 이 PC(127.0.0.1)에서 온 접속만 받고, 페어링 키가 맞는 요청에만 응답한다.
//
//   GET /v1/ping                 연결과 페어링 키 확인
//   GET /v1/subtitles?after=<n>  n번 이후의 자막. 없으면 새 자막이 나올 때까지 잠시 기다린다.
//                                after를 생략하면 현재 마지막 번호만 알려 준다.
class SubtitleServer {
public:
    SubtitleServer();
    ~SubtitleServer();

    SubtitleServer(const SubtitleServer&) = delete;
    SubtitleServer& operator=(const SubtitleServer&) = delete;

    bool start(const ServerConfig& config);
    void stop();

    // 어느 스레드에서 호출해도 된다.
    void publish(Subtitle subtitle);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
