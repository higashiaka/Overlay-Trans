#pragma once

#include <functional>
#include <memory>
#include <string>

namespace overlay_trans {

struct TrayConfig {
    std::wstring tooltip;
    // "페어링 코드 복사" 메뉴로 클립보드에 넣을 값.
    std::wstring pairing_code;
    // "종료" 메뉴를 누르면 트레이 스레드에서 호출된다.
    std::function<void()> on_quit;
};

// 작업 표시줄 알림 영역의 아이콘. 페어링 코드 복사, 로그(콘솔) 창 보이기·숨기기, 종료 메뉴를 제공한다.
// 아이콘의 메시지 처리는 따로 만든 스레드에서 한다.
class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool start(const TrayConfig& config);
    void stop();

    // 알림 영역에 풍선 알림을 띄운다.
    void notify(const std::wstring& title, const std::wstring& text);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 이 프로세스의 콘솔 창을 보이거나 숨긴다.
void set_console_visible(bool visible);

}  // namespace overlay_trans
