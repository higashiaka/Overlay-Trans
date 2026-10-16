#include "net/winhttp_client.h"

#define NOMINMAX
#include <windows.h>

#include <winhttp.h>

#include <vector>

namespace overlay_trans {

namespace {

// 응답이 이보다 오래 오지 않으면 실패로 본다. 자막으로는 이미 늦은 시간이다.
constexpr int RESOLVE_TIMEOUT_MS = 5000;
constexpr int CONNECT_TIMEOUT_MS = 5000;
constexpr int SEND_TIMEOUT_MS = 10000;
constexpr int RECEIVE_TIMEOUT_MS = 15000;

std::wstring to_wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

// 요청 하나의 핸들. 범위를 벗어나면 닫는다.
struct RequestHandle {
    HINTERNET handle = nullptr;
    ~RequestHandle() {
        if (handle != nullptr) {
            WinHttpCloseHandle(handle);
        }
    }
};

}  // namespace

WinHttpClient::WinHttpClient() {
    session_ = WinHttpOpen(L"OverlayTrans", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_ != nullptr) {
        WinHttpSetTimeouts(session_, RESOLVE_TIMEOUT_MS, CONNECT_TIMEOUT_MS, SEND_TIMEOUT_MS, RECEIVE_TIMEOUT_MS);
    }
}

WinHttpClient::~WinHttpClient() {
    if (connection_ != nullptr) {
        WinHttpCloseHandle(connection_);
    }
    if (session_ != nullptr) {
        WinHttpCloseHandle(session_);
    }
}

int WinHttpClient::post(const HttpsRequest& request, const DataCallback& on_data) {
    if (session_ == nullptr) {
        return 0;
    }
    if (connection_ == nullptr || connected_host_ != request.host) {
        if (connection_ != nullptr) {
            WinHttpCloseHandle(connection_);
        }
        connection_ = WinHttpConnect(session_, to_wide(request.host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        connected_host_ = connection_ != nullptr ? request.host : std::string();
        if (connection_ == nullptr) {
            return 0;
        }
    }

    RequestHandle handle{WinHttpOpenRequest(connection_, L"POST", to_wide(request.path).c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (handle.handle == nullptr) {
        return 0;
    }

    std::string headers;
    for (const auto& [name, value] : request.headers) {
        headers += name + ": " + value + "\r\n";
    }
    const std::wstring wide_headers = to_wide(headers);
    const auto body_size = static_cast<DWORD>(request.body.size());
    if (!WinHttpSendRequest(handle.handle, wide_headers.c_str(), static_cast<DWORD>(-1L),
                            const_cast<char*>(request.body.data()), body_size, body_size, 0) ||
        !WinHttpReceiveResponse(handle.handle, nullptr)) {
        return 0;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(handle.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        return 0;
    }

    std::vector<char> buffer;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(handle.handle, &available)) {
            return 0;
        }
        if (available == 0) {
            break;
        }
        buffer.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(handle.handle, buffer.data(), available, &read)) {
            return 0;
        }
        if (on_data) {
            on_data(std::string_view(buffer.data(), read));
        }
    }
    return static_cast<int>(status);
}

}  // namespace overlay_trans
