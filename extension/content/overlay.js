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
  // 화면에 떠 있는 자막. 자막 번호로 찾아서, 말이 이어져 다시 번역된 자막이 오면 내용을 바꾼다.
  const visibleLines = new Map();

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

    show({ sequence, replaces, source, translation }) {
      // 바꿀 자막이 아직 화면에 있으면 그 자리에서 내용만 바꾸고, 이미 사라졌으면 새 자막으로 띄운다.
      let entry = visibleLines.get(replaces);
      if (entry?.element.isConnected) {
        visibleLines.delete(replaces);
        clearTimeout(entry.timer);
      } else {
        visibleLines.delete(replaces);
        const element = document.createElement("div");
        element.className = "overlay-trans-line";

        const translationElement = document.createElement("div");
        translationElement.className = "overlay-trans-translation";
        element.appendChild(translationElement);

        const sourceElement = document.createElement("div");
        sourceElement.className = "overlay-trans-source";
        element.appendChild(sourceElement);

        root.appendChild(element);
        while (root.children.length > MAX_LINES) {
          root.firstElementChild.remove();
        }
        entry = { element, translationElement, sourceElement, timer: null };
      }

      entry.translationElement.textContent = translation;
      entry.sourceElement.textContent = source;
      entry.timer = setTimeout(() => {
        entry.element.remove();
        visibleLines.delete(sequence);
      }, visibleDuration(translation));
      visibleLines.set(sequence, entry);
    },
  };
})();
