(() => {
  const form = document.querySelector("#custom-site-form");
  const input = document.querySelector("#custom-site-url");
  const mode = document.querySelector("#custom-site-mode");
  const add = document.querySelector("#custom-site-add");
  const list = document.querySelector("#custom-site-list");
  const result = document.querySelector("#custom-site-result");
  const prefix = "custom-site-";
  const builtIn = ["https://www.twitch.tv/*", "https://www.youtube.com/*"];
  const audioScript = "content/sites/audio.js";

  function sitePattern(value) {
    const address = value.trim();
    const url = new URL(address.includes("://") ? address : `https://${address}`);
    if (!["http:", "https:"].includes(url.protocol) || !url.hostname ||
        url.hostname.includes("*") || url.username || url.password) {
      throw new Error("Invalid site");
    }
    // 호스트 권한과 콘텐츠 스크립트는 호스트의 모든 경로와 포트에 적용된다.
    return `${url.protocol}//${url.hostname}/*`;
  }

  async function getSites() {
    return (await chrome.scripting.getRegisteredContentScripts())
      .filter(({ id }) => id.startsWith(prefix));
  }

  async function renderSites() {
    const sites = await getSites();
    list.replaceChildren(...sites.map(({ id, js, matches: [pattern] }) => {
      const item = document.createElement("li");
      const label = document.createElement("span");
      label.textContent = `${pattern.slice(0, -2)} · ${js.includes(audioScript) ? "음성 전용" : "영상"}`;
      const remove = document.createElement("button");
      remove.type = "button";
      remove.textContent = "삭제";
      remove.setAttribute("aria-label", `${label.textContent} 삭제`);
      remove.addEventListener("click", () => updateSites(async () => {
        await chrome.permissions.remove({ origins: [pattern] });
        await chrome.scripting.unregisterContentScripts({ ids: [id] });
        return "삭제했습니다. 열려 있는 탭은 새로고침해 주세요.";
      }));
      item.append(label, remove);
      return item;
    }));
  }

  async function updateSites(action) {
    if (add.disabled) return;
    add.disabled = true;
    for (const button of list.querySelectorAll("button")) button.disabled = true;
    try {
      result.textContent = await action();
      await renderSites();
    } catch {
      result.textContent = "사이트 설정을 변경하지 못했습니다. 다시 시도해 주세요.";
    } finally {
      add.disabled = false;
      for (const button of list.querySelectorAll("button")) button.disabled = false;
    }
  }

  form.addEventListener("submit", (event) => {
    event.preventDefault();
    let pattern;
    try {
      pattern = sitePattern(input.value);
    } catch {
      result.textContent = "올바른 웹사이트 주소를 입력해 주세요.";
      return;
    }
    if (builtIn.includes(pattern)) {
      result.textContent = "기본으로 지원하는 사이트입니다.";
      return;
    }

    const scripts = [
      mode.value === "audio" ? audioScript : "content/sites/custom.js",
      "content/overlay.js",
      "content/main.js",
    ];
    updateSites(async () => {
      const origins = [pattern];
      // 권한 요청은 폼 제출 이벤트에서 바로 실행해야 한다.
      if (!await chrome.permissions.request({ origins })) {
        return "사이트 접근을 허용하면 자막을 표시할 수 있습니다.";
      }
      const sites = await getSites();
      const existing = sites.find(({ matches }) => matches.includes(pattern));
      if (existing) {
        await chrome.scripting.updateContentScripts([{ id: existing.id, js: scripts }]);
      } else {
        try {
          await chrome.scripting.registerContentScripts([{
            id: prefix + crypto.randomUUID(),
            matches: origins,
            js: scripts,
            css: ["content/overlay.css"],
          }]);
        } catch (error) {
          await chrome.permissions.remove({ origins });
          throw error;
        }
      }
      input.value = "";
      return "등록했습니다. 해당 사이트를 열거나 새로고침한 뒤 영상을 재생하세요.";
    });
  });

  renderSites().catch(() => {
    result.textContent = "사이트 목록을 불러오지 못했습니다.";
  });
})();
