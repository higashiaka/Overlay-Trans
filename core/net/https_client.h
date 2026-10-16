#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace overlay_trans {

struct HttpsRequest {
    std::string host;  // 예: api.anthropic.com
    std::string path;  // 예: /v1/messages
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

// HTTPS 요청을 보내는 부분. 운영체제마다 쓸 수 있는 TLS 구현이 달라서 플랫폼 쪽에서 만들어 넘긴다.
class HttpsClient {
public:
    // 응답 본문을 받는 대로 조금씩 넘긴다.
    using DataCallback = std::function<void(std::string_view data)>;

    virtual ~HttpsClient() = default;

    // POST 요청을 보내고 HTTP 상태 코드를 반환한다. 연결하지 못했으면 0을 반환한다.
    virtual int post(const HttpsRequest& request, const DataCallback& on_data) = 0;
};

}  // namespace overlay_trans
