// 트위치 화면 구조에 의존하는 코드는 이 파일에만 둔다.
// 트위치가 개편되면 이 파일의 선택자만 고치면 되고, 다른 사이트는 같은 모양의 파일을 추가하면 된다.

globalThis.OverlayTrans = globalThis.OverlayTrans ?? {};

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
};
