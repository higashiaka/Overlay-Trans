// 유튜브 화면 구조에 의존하는 코드는 이 파일에만 둔다.
// 유튜브가 개편되면 이 파일의 선택자만 고치면 된다. (twitch.js와 같은 모양이다.)

globalThis.OverlayTrans = globalThis.OverlayTrans ?? {};

(() => {
  const TITLE_SUFFIX = " - YouTube";

  function text(selector) {
    return document.querySelector(selector)?.textContent?.trim() ?? "";
  }

  // 영상을 보는 화면인지. 목록이나 검색 화면에서는 방송 정보를 보내지 않는다.
  function isWatchPage() {
    return location.pathname === "/watch" || location.pathname.startsWith("/live/");
  }

  // 실시간 채팅은 같은 사이트의 다른 문서(iframe)에 들어 있다.
  function chatDocument() {
    try {
      return document.querySelector("iframe#chatframe")?.contentDocument ?? null;
    } catch {
      return null;
    }
  }

  const CHAT_LINE = "yt-live-chat-text-message-renderer";
  const CHAT_NAME = "#author-name";
  const CHAT_BODY = "#message";

  // 이미 읽은 채팅 줄. 같은 채팅을 두 번 보내지 않기 위해 기억해 둔다.
  const seenChatLines = new WeakSet();
  let skippedBacklog = false;

  globalThis.OverlayTrans.site = {
    name: "youtube",

    // 자막을 올려 둘 영상 플레이어 요소. 전체 화면에서도 이 요소가 화면을 채우므로 자막이 함께 보인다.
    findPlayer() {
      if (!isWatchPage()) {
        return null;
      }
      const player = document.querySelector("#movie_player");
      if (player) {
        return player;
      }
      // 선택자가 바뀌었을 때를 대비해, 영상 요소의 부모를 대신 쓴다.
      return document.querySelector("video")?.parentElement ?? null;
    },

    // 방송 정보. 찾지 못한 항목은 빈 문자열로 둔다. 유튜브 화면에는 게임(카테고리) 표시가 따로 없다.
    getStreamInfo() {
      if (!isWatchPage()) {
        return { channel: "", title: "", category: "" };
      }

      let title = text("ytd-watch-metadata h1");
      if (!title && document.title.endsWith(TITLE_SUFFIX)) {
        // 탭 제목은 "(알림 수) 영상 제목 - YouTube" 형식이다.
        title = document.title.slice(0, -TITLE_SUFFIX.length).replace(/^\(\d+\)\s*/, "");
      }
      return {
        channel: text("ytd-watch-metadata ytd-channel-name #text") || text("#owner ytd-channel-name"),
        title,
        category: "",
      };
    },

    // 지난번 호출 이후 새로 올라온 채팅. 페이지를 열었을 때 이미 쌓여 있던 채팅은 건너뛴다.
    getNewChatMessages() {
      const chat = chatDocument();
      if (!chat) {
        return [];
      }

      const messages = [];
      for (const line of chat.querySelectorAll(CHAT_LINE)) {
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

    // 지금 화면에서 찾은 채팅 줄 수. 0이면 채팅이 없는 영상이거나 채팅 요소 이름이 실제 화면과 맞지 않는 것이다.
    countChatLines() {
      return chatDocument()?.querySelectorAll(CHAT_LINE).length ?? 0;
    },
  };
})();
