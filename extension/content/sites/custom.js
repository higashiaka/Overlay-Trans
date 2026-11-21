globalThis.OverlayTrans = globalThis.OverlayTrans ?? {};

globalThis.OverlayTrans.site = {
  findVideo() {
    const scope = document.fullscreenElement ?? document;
    let primary = null;
    let largestArea = 0;
    for (const video of scope.querySelectorAll("video")) {
      if (video.paused || video.ended || video.readyState < 2 ||
          getComputedStyle(video).visibility !== "visible") {
        continue;
      }
      const rect = video.getBoundingClientRect();
      const visibleWidth = Math.min(rect.right, innerWidth) - Math.max(rect.left, 0);
      const visibleHeight = Math.min(rect.bottom, innerHeight) - Math.max(rect.top, 0);
      if (visibleWidth <= 0 || visibleHeight <= 0) continue;
      const area = visibleWidth * visibleHeight;
      if (area > largestArea) {
        primary = video;
        largestArea = area;
      }
    }
    return primary;
  },
};
