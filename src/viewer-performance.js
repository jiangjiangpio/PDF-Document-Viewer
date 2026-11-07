// Pure rendering helpers; independent of PDF.js and Electron.
export class ViewportAnchor {
  constructor(page, offset, fraction = 0.45) {
    this.page = page; this.offset = offset; this.fraction = fraction;
  }
  static capture(viewer, pages, fraction = 0.45) {
    const y = viewer.getBoundingClientRect().top + viewer.clientHeight * fraction;
    let closest = null, distance = Infinity;
    for (const [page, node] of pages) {
      const r = node.getBoundingClientRect();
      const d = Math.max(r.top - y, y - r.bottom, 0);
      if (d < distance) {
        closest = new ViewportAnchor(page, Math.max(0, Math.min(1, (y - r.top) / Math.max(1, r.height))), fraction);
        distance = d;
      }
      if (d === 0) break;
    }
    return closest;
  }
  restore(viewer, pages) {
    const node = pages.get(this.page);
    if (!node) return;
    const r = node.getBoundingClientRect();
    viewer.scrollTop += r.top - viewer.getBoundingClientRect().top
      + this.offset * r.height - viewer.clientHeight * this.fraction;
  }
}
export class FrameScheduler {
  constructor(callback, request = requestAnimationFrame, cancel = cancelAnimationFrame) {
    this.callback = callback; this.request = request; this.cancelRequest = cancel; this.id = null;
  }
  schedule() {
    if (this.id !== null) return;
    this.id = this.request(() => { this.id = null; this.callback(); });
  }
  cancel() {
    if (this.id !== null) this.cancelRequest(this.id);
    this.id = null;
  }
}
export function boundedOutputScale(width, height, dpr = 1, maxPixels = 6000000) {
  return Math.min(Math.max(1, dpr), 2, Math.sqrt(maxPixels / Math.max(1, width * height)));
}
