#include "tray_icon.h"

#define NOMINMAX
#include <windows.h>

#include <shellapi.h>

#include <cstring>
#include <cwchar>
#include <future>
#include <thread>
#include <utility>

namespace overlay_trans {

namespace {

constexpr wchar_t WINDOW_CLASS[] = L"OverlayTransTray";
constexpr UINT ICON_ID = 1;
constexpr UINT TRAY_MESSAGE = WM_APP + 1;
constexpr UINT NOTIFY_MESSAGE = WM_APP + 2;

enum MenuCommand : UINT {
    COPY_PAIRING_CODE = 1,
    TOGGLE_CONSOLE,
    QUIT,
};

bool is_console_visible() {
    const HWND console = GetConsoleWindow();
    return console != nullptr && IsWindowVisible(console);
}

void copy_to_clipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) {
        return;
    }
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        std::memcpy(GlobalLock(memory), text.c_str(), bytes);
        GlobalUnlock(memory);
        // 성공하면 메모리는 클립보드가 가져간다.
        if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

}  // namespace

struct TrayIcon::Impl {
    TrayConfig config;
    std::jthread thread;
    HWND window = nullptr;
    UINT taskbar_created = 0;  // 탐색기가 다시 시작되면 받는 메시지. 아이콘을 다시 등록해야 한다.
    std::wstring pending_title;
    std::wstring pending_text;

    void run(std::promise<bool>& ready);
    void add_icon();
    void show_menu();
    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam);

    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
};

LRESULT CALLBACK TrayIcon::Impl::window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        // CreateWindowExW가 끝나기 전에도 메시지가 오므로 창 핸들을 여기서 먼저 기억해 둔다.
        static_cast<Impl*>(create->lpCreateParams)->window = window;
    }
    auto* impl = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (impl == nullptr) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    return impl->handle(message, wparam, lparam);
}

void TrayIcon::Impl::add_icon() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = ICON_ID;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = TRAY_MESSAGE;
    data.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcsncpy_s(data.szTip, config.tooltip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &data);
}

void TrayIcon::Impl::show_menu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, COPY_PAIRING_CODE, L"페어링 코드 복사");
    if (GetConsoleWindow() != nullptr) {
        AppendMenuW(menu, MF_STRING, TOGGLE_CONSOLE, is_console_visible() ? L"로그 창 숨기기" : L"로그 창 보이기");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, QUIT, L"종료");

    // 메뉴 밖을 누르면 메뉴가 닫히도록 창을 앞으로 가져온다. (Windows 알림 영역 메뉴의 정해진 사용법)
    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window, nullptr);
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

LRESULT TrayIcon::Impl::handle(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == TRAY_MESSAGE) {
        if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
            show_menu();
        } else if (lparam == WM_LBUTTONDBLCLK) {
            set_console_visible(!is_console_visible());
        }
        return 0;
    }
    if (message == NOTIFY_MESSAGE) {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = ICON_ID;
        data.uFlags = NIF_INFO;
        data.dwInfoFlags = NIIF_INFO;
        wcsncpy_s(data.szInfoTitle, pending_title.c_str(), _TRUNCATE);
        wcsncpy_s(data.szInfo, pending_text.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &data);
        return 0;
    }
    if (message == taskbar_created) {
        add_icon();
        return 0;
    }

    switch (message) {
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case COPY_PAIRING_CODE:
            copy_to_clipboard(window, config.pairing_code);
            break;
        case TOGGLE_CONSOLE:
            set_console_visible(!is_console_visible());
            break;
        case QUIT:
            if (config.on_quit) {
                config.on_quit();
            }
            break;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY: {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = ICON_ID;
        Shell_NotifyIconW(NIM_DELETE, &data);
        PostQuitMessage(0);
        return 0;
    }
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

void TrayIcon::Impl::run(std::promise<bool>& ready) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = WINDOW_CLASS;
    RegisterClassExW(&window_class);

    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    // 알림 영역 아이콘의 메시지를 받기만 하는 창이다. 화면에 보이지 않는다.
    window = CreateWindowExW(0, WINDOW_CLASS, L"OverLay-Trans", 0, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (window == nullptr) {
        ready.set_value(false);
        return;
    }
    add_icon();
    ready.set_value(true);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    window = nullptr;
}

TrayIcon::TrayIcon() : impl_(std::make_unique<Impl>()) {}

TrayIcon::~TrayIcon() {
    stop();
}

bool TrayIcon::start(const TrayConfig& config) {
    impl_->config = config;
    std::promise<bool> ready;
    std::future<bool> started = ready.get_future();
    impl_->thread = std::jthread([this, &ready] { impl_->run(ready); });
    return started.get();
}

void TrayIcon::stop() {
    if (impl_->window != nullptr) {
        PostMessageW(impl_->window, WM_CLOSE, 0, 0);
    }
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

void TrayIcon::notify(const std::wstring& title, const std::wstring& text) {
    if (impl_->window == nullptr) {
        return;
    }
    // 아이콘은 트레이 스레드에서만 바꾼다. 내용을 넘겨 두고 그 스레드에 알린다.
    impl_->pending_title = title;
    impl_->pending_text = text;
    PostMessageW(impl_->window, NOTIFY_MESSAGE, 0, 0);
}

void set_console_visible(bool visible) {
    const HWND console = GetConsoleWindow();
    if (console == nullptr) {
        return;
    }
    ShowWindow(console, visible ? SW_SHOW : SW_HIDE);
    if (visible) {
        SetForegroundWindow(console);
    }
}

}  // namespace overlay_trans
