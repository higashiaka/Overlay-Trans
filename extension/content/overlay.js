// 영상 플레이어 위에 자막을 그린다. 사이트와 무관한 공통 코드다.

globalThis.OverlayTrans = globalThis.OverlayTrans ?? {};

(() => {
  const MAX_LINES = 2;
  const MIN_VISIBLE_MS = 3500;
  const MAX_VISIBLE_MS = 9000;
  const VISIBLE_MS_PER_CHARACTER = 120;

  const root = document.createElement("div");
  root.id = "overlay-trans-root";

  let showSource = true;

  function visibleDuration(text) {
    return Math.min(MAX_VISIBLE_MS, Math.max(MIN_VISIBLE_MS, text.length * VISIBLE_MS_PER_CHARACTER));
  }

  globalThis.OverlayTrans.overlay = {
    // 플레이어가 바뀌었거나 페이지 이동으로 자막 영역이 떨어져 나갔으면 다시 붙인다.
    attach(player) {
      if (player && root.parentElement !== player) {
        player.appendChild(root);
      }
    },

    setShowSource(value) {
      showSource = value;
      root.classList.toggle("overlay-trans-hide-source", !showSource);
    },

    show({ source, translation }) {
      const line = document.createElement("div");
      line.className = "overlay-trans-line";

      const translationElement = document.createElement("div");
      translationElement.className = "overlay-trans-translation";
      translationElement.textContent = translation;
      line.appendChild(translationElement);

      const sourceElement = document.createElement("div");
      sourceElement.className = "overlay-trans-source";
      sourceElement.textContent = source;
      line.appendChild(sourceElement);

      root.appendChild(line);
      while (root.children.length > MAX_LINES) {
        root.firstElementChild.remove();
      }
      setTimeout(() => line.remove(), visibleDuration(translation));
    },
  };
})();
