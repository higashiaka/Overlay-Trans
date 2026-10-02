// 방송 페이지에서 실행된다. 서비스 워커에서 자막을 받아 플레이어 위에 표시한다.

(() => {
  const { site, overlay } = globalThis.OverlayTrans;

  const RECONNECT_DELAY_MS = 1000;
  // 서비스 워커는 30초 동안 아무 일이 없으면 종료되므로, 그보다 짧은 간격으로 메시지를 보낸다.
  const KEEPALIVE_INTERVAL_MS = 20000;
  // 트위치는 페이지를 새로 읽지 않고 화면을 바꾸므로, 플레이어가 바뀌었는지 주기적으로 확인한다.
  const ATTACH_INTERVAL_MS = 1000;

  let port = null;

  function connect() {
    try {
      port = chrome.runtime.connect({ name: "subtitles" });
    } catch {
      // 확장 프로그램이 다시 설치되거나 갱신되면 이 페이지의 스크립트는 더 이상 연결할 수 없다.
      // 페이지를 새로 고치면 새 스크립트가 실행된다.
      port = null;
      return;
    }

    port.onMessage.addListener((message) => {
      if (message.type === "subtitle") {
        overlay.show(message);
      }
    });
    port.onDisconnect.addListener(() => {
      port = null;
      setTimeout(connect, RECONNECT_DELAY_MS);
    });
  }

  chrome.storage.local.get({ showSource: true }).then(({ showSource }) => overlay.setShowSource(showSource));
  chrome.storage.onChanged.addListener((changes) => {
    if (changes.showSource) {
      overlay.setShowSource(changes.showSource.newValue);
    }
  });

  connect();
  setInterval(() => {
    try {
      port?.postMessage({ type: "keepalive" });
    } catch {
      port = null;
    }
  }, KEEPALIVE_INTERVAL_MS);
  setInterval(() => overlay.attach(site.findPlayer()), ATTACH_INTERVAL_MS);
})();
