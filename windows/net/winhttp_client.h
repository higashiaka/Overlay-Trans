#pragma once

#include "net/https_client.h"

#include <string>

namespace overlay_trans {

// WinHTTP로 HTTPS 요청을 보낸다. 인증서 확인과 프록시 설정은 Windows의 것을 그대로 쓴다.
// 연결을 다시 쓸 수 있도록 세션을 유지한다. 한 스레드에서만 써야 한다.
class WinHttpClient final : public HttpsClient {
public:
    WinHttpClient();
    ~WinHttpClient() override;

    WinHttpClient(const WinHttpClient&) = delete;
    WinHttpClient& operator=(const WinHttpClient&) = delete;

    int post(const HttpsRequest& request, const DataCallback& on_data) override;

private:
    void* session_ = nullptr;
    void* connection_ = nullptr;
    std::string connected_host_;  // connection_이 연결된 호스트
};

}  // namespace overlay_trans
