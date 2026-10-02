// 트위치 화면 구조에 의존하는 코드는 이 파일에만 둔다.
// 트위치가 개편되면 이 파일의 선택자만 고치면 되고, 다른 사이트는 같은 모양의 파일을 추가하면 된다.

globalThis.OverlayTrans = globalThis.OverlayTrans ?? {};

(() => {
  // 주소의 첫 칸이 채널 이름이 아닌 경우들.
  const NON_CHANNEL_PATHS = new Set(["", "directory", "videos", "settings", "search", "downloads", "subscriptions"]);

  function text(selector) {
    return document.querySelector(selector)?.textContent?.trim() ?? "";
  }

  // 채팅 요소. 쉼표 뒤는 채팅 창을 바꿔 그리는 확장 프로그램(7TV)을 쓸 때의 요소 이름이다.
  const CHAT_LINE = ".chat-line__message, .seventv-message";
  const CHAT_NAME = ".chat-author__display-name, .seventv-chat-user-username";
  const CHAT_BODY = '[data-a-target="chat-line-message-body"], .seventv-chat-message-body';

  // 이미 읽은 채팅 줄. 같은 채팅을 두 번 보내지 않기 위해 기억해 둔다.
  const seenChatLines = new WeakSet();
  let skippedBacklog = false;

  globalThis.OverlayTrans.site = {
    name: "twitch",

    // 자막을 올려 둘 영상 플레이어 요소. 전체 화면에서도 자막이 보이도록 플레이어 컨테이너를 찾는다.
    findPlayer() {
      const player =
        document.querySelector(".video-player__container") ??
        document.querySelector('[data-a-target="video-player"]');
      if (player) {
        return player;
      }
      // 선택자가 바뀌었을 때를 대비해, 영상 요소의 부모를 대신 쓴다.
      return document.querySelector("video")?.parentElement ?? null;
    },

    // 방송 정보. 찾지 못한 항목은 빈 문자열로 둔다.
    getStreamInfo() {
      const firstPath = location.pathname.split("/")[1] ?? "";
      return {
        channel: NON_CHANNEL_PATHS.has(firstPath) ? "" : firstPath,
        title: text('[data-a-target="stream-title"]'),
        category: text('[data-a-target="stream-game-link"]'),
      };
    },

    // 지난번 호출 이후 새로 올라온 채팅. 페이지를 열었을 때 이미 쌓여 있던 채팅은 건너뛴다.
    getNewChatMessages() {
      const messages = [];
      for (const line of document.querySelectorAll(CHAT_LINE)) {
        if (seenChatLines.has(line)) {
          continue;
        }
        seenChatLines.add(line);

        const name = line.querySelector(CHAT_NAME)?.textContent?.trim() ?? "";
        const body = line.querySelector(CHAT_BODY)?.textContent?.trim() ?? "";
        if (body) {
          messages.push({ name, text: body });
        }
      }

      if (!skippedBacklog) {
        skippedBacklog = true;
        return [];
      }
      return messages;
    },

    // 지금 화면에서 찾은 채팅 줄 수. 0이면 채팅 요소 이름이 실제 화면과 맞지 않는 것이다.
    countChatLines() {
      return document.querySelectorAll(CHAT_LINE).length;
    },
  };
})();
