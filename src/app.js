import * as pdfjs from "pdfjs-dist/build/pdf.mjs";
import pdfWorkerUrl from "pdfjs-dist/build/pdf.worker.min.mjs?url";
import "./styles.css";

pdfjs.GlobalWorkerOptions.workerSrc = pdfWorkerUrl;
const api = window.viewerAPI;
const $ = (id) => document.getElementById(id);
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const state = {
  docs: [], active: null, counter: 0, tool: "hand",
  color: "#355bda", penWidth: 3, sidebar: true, sideMode: "thumbs",
  viewGeneration: 0, renderQueue: [], renderBusy: 0,
  thumbQueue: [], thumbBusy: false, observer: null, thumbObserver: null,
  visible: new Map(), lastPenAt: 0, searchToken: 0
};
const viewer = $("pageScroll");
const pageList = $("pageList");
const sidebarContent = $("sidebarContent");
let toastTimer;

function toast(message, isError = false) {
  const el = $("toast");
  el.textContent = message;
  el.className = isError ? "show error" : "show";
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.className = ""; }, 3500);
}
function currentDoc() { return state.docs.find((doc) => doc.id === state.active) || null; }
function formatName(name) { return name.length > 44 ? name.slice(0, 41) + "…" : name; }

async function openDialog() {
  try {
    if (api) {
      const items = await api.pickPdf();
      for (const item of items) await openPdf(item);
    } else $("fallbackFile").click();
  } catch (error) { toast("打开文件失败：" + error.message, true); }
}
async function openPdf({ path = null, name, bytes }) {
  if (path) {
    const existing = state.docs.find((doc) => doc.path === path);
    if (existing) { await activate(existing.id); return; }
  }
  toast("正在解析 PDF…");
  let task;
  try {
    const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    task = pdfjs.getDocument({
      data,
      cMapUrl: new URL("./cmaps/", window.location.href).href,
      cMapPacked: true,
      standardFontDataUrl: new URL("./standard_fonts/", window.location.href).href,
      wasmUrl: new URL("./wasm/", window.location.href).href,
      useSystemFonts: true,
      enableXfa: true,
      isEvalSupported: false,
      stopAtErrors: false
    });
    const pdf = await task.promise;
    const first = await pdf.getPage(1);
    const firstViewport = first.getViewport({ scale: 1 });
    const ink = path && api ? await api.loadInk(path) : { version: 1, pages: {} };
    const doc = {
      id: ++state.counter, path, name, pdf, task,
      ink: ink?.version === 1 && ink.pages ? ink : { version: 1, pages: {} },
      firstSize: { width: firstViewport.width, height: firstViewport.height },
      pageSizes: new Map(), pageNodes: new Map(), renderTasks: new Map(),
      rendered: new Set(), undo: [], redo: [], dirty: false, revision: 0,
      current: 1, fit: "width", scale: 1, scrollTop: 0, searchResults: [], searchIndex: -1
    };
    doc.pageSizes.set(1, doc.firstSize);
    state.docs.push(doc);
    await activate(doc.id);
    toast("已打开 " + doc.name);
  } catch (error) {
    if (task) await task.destroy().catch(() => {});
    toast("无法读取 PDF：" + (error?.message || error), true);
  }
}
async function activate(id) {
  const previous = currentDoc();
  if (previous && previous.id !== id) {
    previous.scrollTop = viewer.scrollTop;
    if (previous.dirty) void saveInk(previous);
  }
  state.active = id;
  state.searchToken++;
  renderTabs();
  await showDocument();
}
function renderTabs() {
  const tabs = $("tabs");
  tabs.replaceChildren();
  for (const doc of state.docs) {
    const tab = document.createElement("div");
    tab.className = "document-tab" + (doc.id === state.active ? " active" : "");
    tab.dataset.doc = String(doc.id);
    tab.title = doc.name;
    const icon = document.createElement("span"); icon.className = "pdf-tab-icon"; icon.textContent = "PDF";
    const label = document.createElement("span"); label.className = "tab-label"; label.textContent = formatName(doc.name);
    tab.append(icon, label);
    if (doc.dirty) { const dot = document.createElement("span"); dot.className = "unsaved-dot"; tab.append(dot); }
    const close = document.createElement("button");
    close.className = "close-tab"; close.dataset.close = String(doc.id); close.title = "关闭"; close.textContent = "×";
    tab.append(close); tabs.append(tab);
  }
}
async function closeDoc(id) {
  const index = state.docs.findIndex((doc) => doc.id === id);
  if (index < 0) return;
  const doc = state.docs[index];
  clearTimeout(doc.saveTimer);
  if (doc.dirty) await saveInk(doc);
  for (const task of doc.renderTasks.values()) task.cancel();
  state.docs.splice(index, 1);
  if (state.active === id) {
    state.active = state.docs[Math.min(index, state.docs.length - 1)]?.id ?? null;
    await showDocument();
  }
  void doc.task.destroy().catch(() => {});
  renderTabs();
}
function setTool(tool) {
  state.tool = tool;
  document.querySelectorAll("[data-tool]").forEach((el) => el.classList.toggle("active", el.dataset.tool === tool));
  $("readerShell").dataset.tool = tool;
  document.querySelectorAll(".ink-layer").forEach((canvas) => {
    canvas.style.pointerEvents = tool === "hand" ? "none" : "auto";
  });
}
function setSidebar(visible) {
  state.sidebar = visible;
  $("readerShell").classList.toggle("sidebar-hidden", !visible);
  const doc = currentDoc();
  if (doc && doc.fit !== "manual") setTimeout(() => changeZoom(doc.fit), 50);
}
function calcScale(doc, fit) {
  const bounds = viewer.getBoundingClientRect();
  const availableWidth = Math.max(240, bounds.width - 92);
  const availableHeight = Math.max(250, bounds.height - 56);
  if (fit === "width") return clamp(availableWidth / doc.firstSize.width, 0.25, 4);
  if (fit === "page") return clamp(Math.min(availableWidth / doc.firstSize.width, availableHeight / doc.firstSize.height), 0.25, 4);
  return doc.scale;
}
function changeZoom(modeOrScale) {
  const doc = currentDoc();
  if (!doc) return;
  const page = doc.current;
  if (typeof modeOrScale === "string") {
    doc.fit = modeOrScale;
    doc.scale = calcScale(doc, modeOrScale);
  } else {
    doc.fit = "manual";
    doc.scale = clamp(modeOrScale, 0.25, 4);
  }
  buildPages(doc);
  goToPage(page, false);
  updateStatus();
}
function updateStatus() {
  const doc = currentDoc();
  $("statusName").textContent = doc ? doc.name : "未打开文档";
  $("saveStatus").textContent = doc?.dirty ? "批注待保存…" : "批注已独立保存";
  $("pageNumber").value = doc?.current || 1;
  $("pageNumber").max = doc?.pdf.numPages || 1;
  $("totalPages").textContent = "/ " + (doc?.pdf.numPages || 0);
  $("zoomLabel").textContent = Math.round((doc?.scale || 1) * 100) + "%";
  $("zoomRange").value = Math.round(clamp((doc?.scale || 1) * 100, 25, 300) / 5) * 5;
}
async function showDocument() {
  const doc = currentDoc();
  $("emptyState").classList.toggle("hidden", !!doc);
  viewer.classList.toggle("hidden", !doc);
  $("sidebar").classList.toggle("empty-sidebar", !doc);
  if (!doc) {
    stopObservers();
    pageList.replaceChildren(); sidebarContent.replaceChildren();
    renderTabs(); updateStatus(); return;
  }
  doc.scale = doc.fit === "manual" ? doc.scale : calcScale(doc, doc.fit);
  buildPages(doc);
  await renderSidebar(doc);
  viewer.scrollTop = doc.scrollTop || 0;
  updateStatus();
}
function stopObservers() {
  state.observer?.disconnect(); state.thumbObserver?.disconnect();
  state.observer = null; state.thumbObserver = null;
  state.visible.clear();
  state.viewGeneration++;
  state.renderQueue.length = 0;
  state.thumbQueue.length = 0;
  for (const doc of state.docs) {
    for (const task of doc.renderTasks.values()) task.cancel();
    doc.renderTasks.clear();
  }
}
function buildPages(doc) {
  stopObservers();
  const generation = state.viewGeneration;
  pageList.replaceChildren();
  doc.pageNodes.clear();
  doc.rendered.clear();
  const fragment = document.createDocumentFragment();
  for (let pageNo = 1; pageNo <= doc.pdf.numPages; pageNo++) {
    const size = doc.pageSizes.get(pageNo) || doc.firstSize;
    const node = document.createElement("article");
    node.className = "page";
    node.dataset.page = String(pageNo);
    node.style.width = Math.round(size.width * doc.scale) + "px";
    node.style.height = Math.round(size.height * doc.scale) + "px";
    node.innerHTML = '<div class="page-paper"><canvas class="pdf-layer"></canvas><div class="textLayer"></div><canvas class="ink-layer"></canvas><div class="page-loading">正在渲染…</div></div>';
    node.querySelector(".ink-layer").style.pointerEvents = state.tool === "hand" ? "none" : "auto";
    attachInkEvents(node.querySelector(".ink-layer"), doc, pageNo);
    fragment.appendChild(node);
    doc.pageNodes.set(pageNo, node);
  }
  pageList.appendChild(fragment);
  state.observer = new IntersectionObserver((entries) => {
    if (state.viewGeneration !== generation || currentDoc()?.id !== doc.id) return;
    for (const entry of entries) {
      const pageNo = Number(entry.target.dataset.page);
      if (entry.isIntersecting) {
        state.visible.set(pageNo, entry.intersectionRatio);
        queueRender(doc, pageNo, generation);
      } else {
        state.visible.delete(pageNo);
        const rect = entry.target.getBoundingClientRect();
        const viewerRect = viewer.getBoundingClientRect();
        if (rect.bottom < viewerRect.top - 1200 || rect.top > viewerRect.bottom + 1200) disposePage(doc, pageNo);
      }
    }
    if (state.visible.size) {
      const visible = [...state.visible.keys()];
      const center = viewer.getBoundingClientRect().top + viewer.clientHeight * 0.42;
      visible.sort((a, b) => Math.abs(doc.pageNodes.get(a).getBoundingClientRect().top - center) - Math.abs(doc.pageNodes.get(b).getBoundingClientRect().top - center));
      doc.current = visible[0];
      updateStatus();
      document.querySelectorAll(".thumb-entry").forEach((el) => el.classList.toggle("selected", Number(el.dataset.page) === doc.current));
    }
  }, { root: viewer, rootMargin: "800px 0px", threshold: [0, 0.01, 0.2, 0.5] });
  for (const node of doc.pageNodes.values()) state.observer.observe(node);
}
function queueRender(doc, pageNo, generation) {
  if (doc.rendered.has(pageNo) || doc.renderTasks.has(pageNo)) return;
  if (state.renderQueue.some((item) => item.docId === doc.id && item.pageNo === pageNo && item.generation === generation)) return;
  state.renderQueue.push({ docId: doc.id, pageNo, generation });
  pumpRenders();
}
function pumpRenders() {
  while (state.renderBusy < 2 && state.renderQueue.length) {
    const job = state.renderQueue.shift();
    if (job.generation !== state.viewGeneration || currentDoc()?.id !== job.docId) continue;
    const doc = currentDoc();
    state.renderBusy++;
    renderPage(doc, job.pageNo, job.generation).catch((error) => {
      if (error?.name !== "RenderingCancelledException") console.warn("Page rendering:", error);
    }).finally(() => { state.renderBusy--; pumpRenders(); });
  }
}
async function renderPage(doc, pageNo, generation) {
  const node = doc.pageNodes.get(pageNo);
  if (!node || generation !== state.viewGeneration || currentDoc()?.id !== doc.id || doc.rendered.has(pageNo)) return;
  const page = await doc.pdf.getPage(pageNo);
  if (generation !== state.viewGeneration || currentDoc()?.id !== doc.id) return;
  const viewport = page.getViewport({ scale: doc.scale });
  const base = page.getViewport({ scale: 1 });
  doc.pageSizes.set(pageNo, { width: base.width, height: base.height });
  node.style.width = viewport.width + "px";
  node.style.height = viewport.height + "px";
  const canvas = node.querySelector(".pdf-layer");
  const context = canvas.getContext("2d", { alpha: false });
  const desired = Math.min(window.devicePixelRatio || 1, 2);
  const pixelLimit = Math.sqrt(8_000_000 / Math.max(1, viewport.width * viewport.height));
  const outputScale = clamp(Math.min(desired, pixelLimit), 0.3, 2);
  canvas.width = Math.max(1, Math.round(viewport.width * outputScale));
  canvas.height = Math.max(1, Math.round(viewport.height * outputScale));
  canvas.style.width = viewport.width + "px";
  canvas.style.height = viewport.height + "px";
  const renderTask = page.render({
    canvasContext: context, viewport,
    transform: outputScale === 1 ? null : [outputScale, 0, 0, outputScale, 0, 0],
    background: "#ffffff"
  });
  doc.renderTasks.set(pageNo, renderTask);
  try {
    await renderTask.promise;
    if (generation !== state.viewGeneration || currentDoc()?.id !== doc.id) return;
    doc.rendered.add(pageNo);
    node.querySelector(".page-loading").classList.add("hidden");
    const textLayer = node.querySelector(".textLayer");
    textLayer.style.width = viewport.width + "px";
    textLayer.style.height = viewport.height + "px";
    textLayer.style.setProperty("--scale-factor", String(viewport.scale));
    textLayer.style.setProperty("--total-scale-factor", String(viewport.scale));
    try {
      const layer = new pdfjs.TextLayer({
        textContentSource: page.streamTextContent({ includeMarkedContent: true }),
        container: textLayer, viewport
      });
      await layer.render();
    } catch (error) { console.warn("Text layer:", error); }
    if (generation === state.viewGeneration) paintInk(doc, pageNo);
  } finally {
    doc.renderTasks.delete(pageNo);
    if (generation !== state.viewGeneration) { canvas.width = 0; canvas.height = 0; }
  }
}
function disposePage(doc, pageNo) {
  if (!doc.rendered.has(pageNo)) return;
  const node = doc.pageNodes.get(pageNo);
  if (!node) return;
  doc.rendered.delete(pageNo);
  for (const canvas of node.querySelectorAll("canvas")) { canvas.width = 0; canvas.height = 0; }
  node.querySelector(".textLayer").replaceChildren();
  node.querySelector(".page-loading").classList.remove("hidden");
}
function goToPage(pageNo, smooth = true) {
  const doc = currentDoc();
  if (!doc) return;
  pageNo = clamp(Math.round(pageNo) || 1, 1, doc.pdf.numPages);
  doc.current = pageNo;
  const node = doc.pageNodes.get(pageNo);
  if (node) viewer.scrollTo({ top: node.offsetTop - pageList.offsetTop - 20, behavior: smooth ? "smooth" : "instant" });
  updateStatus();
}
async function renderSidebar(doc) {
  if (state.sideMode === "outline") return renderOutline(doc);
  const count = Math.min(doc.pdf.numPages, 300);
  sidebarContent.innerHTML = '<div class="thumbnail-list">' + Array.from({ length: count }, (_, i) =>
    '<button class="thumb-entry ' + (i + 1 === doc.current ? "selected" : "") + '" data-page="' + (i + 1) + '"><span class="thumb-sheet"><canvas width="86" height="110"></canvas></span><span class="thumb-number">' + (i + 1) + '</span></button>'
  ).join("") + '</div>' + (doc.pdf.numPages > count ? '<p class="thumb-note">显示前 300 页；可在下方页码框跳转任意页面。</p>' : "");
  state.thumbObserver?.disconnect();
  const generation = state.viewGeneration;
  state.thumbObserver = new IntersectionObserver((entries) => {
    if (generation !== state.viewGeneration) return;
    for (const entry of entries) {
      if (entry.isIntersecting) {
        const pageNo = Number(entry.target.dataset.page);
        if (!entry.target.dataset.loaded) {
          entry.target.dataset.loaded = "queued";
          state.thumbQueue.push({ docId: doc.id, pageNo, generation, node: entry.target });
          pumpThumbs();
        }
      }
    }
  }, { root: sidebarContent, rootMargin: "220px" });
  sidebarContent.querySelectorAll(".thumb-entry").forEach((node) => state.thumbObserver.observe(node));
}
async function pumpThumbs() {
  if (state.thumbBusy) return;
  state.thumbBusy = true;
  while (state.thumbQueue.length) {
    const job = state.thumbQueue.shift();
    if (job.generation !== state.viewGeneration || currentDoc()?.id !== job.docId) continue;
    try {
      const doc = currentDoc();
      const page = await doc.pdf.getPage(job.pageNo);
      if (job.generation !== state.viewGeneration) continue;
      const base = page.getViewport({ scale: 1 });
      const viewport = page.getViewport({ scale: Math.min(86 / base.width, 110 / base.height) });
      const canvas = job.node.querySelector("canvas");
      canvas.width = Math.ceil(viewport.width); canvas.height = Math.ceil(viewport.height);
      await page.render({ canvasContext: canvas.getContext("2d"), viewport }).promise;
      job.node.dataset.loaded = "true";
    } catch (error) { console.warn("Thumbnail:", error); }
    await wait(0);
  }
  state.thumbBusy = false;
}
async function renderOutline(doc) {
  sidebarContent.innerHTML = '<div class="outline-loading">正在读取目录…</div>';
  const outline = await doc.pdf.getOutline();
  if (currentDoc()?.id !== doc.id || state.sideMode !== "outline") return;
  if (!outline?.length) { sidebarContent.innerHTML = '<div class="outline-loading">此文档没有内置目录</div>'; return; }
  sidebarContent.replaceChildren();
  async function append(items, depth) {
    for (const item of items) {
      const button = document.createElement("button");
      button.className = "outline-item";
      button.style.paddingLeft = 14 + depth * 14 + "px";
      button.textContent = item.title || "未命名章节";
      button.title = item.title || "";
      button.addEventListener("click", async () => {
        try {
          const dest = typeof item.dest === "string" ? await doc.pdf.getDestination(item.dest) : item.dest;
          if (dest?.[0]) goToPage((await doc.pdf.getPageIndex(dest[0])) + 1);
        } catch (_) { toast("无法定位章节", true); }
      });
      sidebarContent.appendChild(button);
      if (item.items?.length && depth < 8) await append(item.items, depth + 1);
    }
  }
  await append(outline, 0);
}
async function runSearch() {
  const doc = currentDoc();
  if (!doc) return;
  const query = $("searchInput").value.trim().toLocaleLowerCase();
  const token = ++state.searchToken;
  if (!query) { $("searchCount").textContent = ""; return; }
  $("searchCount").textContent = "搜索中…";
  const hits = [];
  try {
    for (let pageNo = 1; pageNo <= doc.pdf.numPages; pageNo++) {
      if (token !== state.searchToken) return;
      const page = await doc.pdf.getPage(pageNo);
      const content = await page.getTextContent();
      const text = content.items.map((item) => item.str || "").join(" ").toLocaleLowerCase();
      if (text.includes(query)) hits.push(pageNo);
      if (pageNo % 10 === 0) {
        $("searchCount").textContent = pageNo + "/" + doc.pdf.numPages;
        await wait(0);
      }
    }
    doc.searchResults = hits; doc.searchIndex = hits.length ? 0 : -1;
    $("searchCount").textContent = hits.length ? "1 / " + hits.length + " 页" : "未找到";
    if (hits.length) goToPage(hits[0]);
  } catch (error) { toast("查找失败：" + error.message, true); }
}
function searchMove(direction) {
  const doc = currentDoc();
  if (!doc?.searchResults.length) return;
  doc.searchIndex = (doc.searchIndex + direction + doc.searchResults.length) % doc.searchResults.length;
  goToPage(doc.searchResults[doc.searchIndex]);
  $("searchCount").textContent = (doc.searchIndex + 1) + " / " + doc.searchResults.length + " 页";
}
function drawStroke(ctx, stroke, width, height, scale) {
  const points = stroke.points;
  if (!Array.isArray(points) || !points.length) return;
  ctx.save();
  ctx.strokeStyle = stroke.color || "#355bda";
  ctx.fillStyle = stroke.color || "#355bda";
  ctx.globalAlpha = stroke.tool === "highlight" ? 0.27 : 1;
  ctx.lineCap = "round"; ctx.lineJoin = "round";
  const basicWidth = (stroke.width || 3) * scale * (stroke.tool === "highlight" ? 4 : 1);
  if (points.length === 1) {
    ctx.beginPath(); ctx.arc(points[0][0] * width, points[0][1] * height, basicWidth / 2, 0, Math.PI * 2); ctx.fill();
  }
  for (let i = 1; i < points.length; i++) {
    const prev = points[i - 1], point = points[i];
    const pressure = stroke.tool === "highlight" ? 1 : clamp(((prev[2] ?? 0.5) + (point[2] ?? 0.5)) / 2, 0.2, 1);
    ctx.lineWidth = basicWidth * (0.65 + 0.55 * pressure);
    ctx.beginPath(); ctx.moveTo(prev[0] * width, prev[1] * height); ctx.lineTo(point[0] * width, point[1] * height); ctx.stroke();
  }
  ctx.restore();
}
function paintInk(doc, pageNo, inProgress = null) {
  const node = doc.pageNodes.get(pageNo);
  if (!node) return;
  const canvas = node.querySelector(".ink-layer");
  const width = node.clientWidth, height = node.clientHeight;
  if (!width || !height) return;
  const dpr = clamp(window.devicePixelRatio || 1, 1, 2);
  const w = Math.round(width * dpr), h = Math.round(height * dpr);
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
  canvas.style.width = width + "px"; canvas.style.height = height + "px";
  const ctx = canvas.getContext("2d");
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.clearRect(0, 0, w, h); ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  for (const stroke of doc.ink.pages[pageNo] || []) drawStroke(ctx, stroke, width, height, doc.scale);
  if (inProgress) drawStroke(ctx, inProgress, width, height, doc.scale);
}
function eraseAt(doc, pageNo, x, y) {
  const strokes = doc.ink.pages[pageNo] || [];
  const node = doc.pageNodes.get(pageNo);
  const threshold = 14;
  for (let index = strokes.length - 1; index >= 0; index--) {
    const stroke = strokes[index];
    if (stroke.points?.some((p) => Math.hypot(p[0] * node.clientWidth - x, p[1] * node.clientHeight - y) < threshold + (stroke.width || 3) * doc.scale / 2)) {
      strokes.splice(index, 1);
      doc.undo.push({ kind: "remove", pageNo, stroke, index });
      doc.redo.length = 0;
      markDirty(doc); paintInk(doc, pageNo);
      return;
    }
  }
}
function attachInkEvents(canvas, doc, pageNo) {
  let drawing = null;
  function point(event) {
    const rect = canvas.getBoundingClientRect();
    return [clamp((event.clientX - rect.left) / rect.width, 0, 1), clamp((event.clientY - rect.top) / rect.height, 0, 1), event.pointerType === "pen" ? (event.pressure || 0.5) : 0.5];
  }
  function shouldIgnore(event) {
    if (currentDoc()?.id !== doc.id || state.tool === "hand") return true;
    if (event.pointerType === "pen") state.lastPenAt = Date.now();
    return event.pointerType === "touch" && Date.now() - state.lastPenAt < 1100;
  }
  canvas.addEventListener("pointerdown", (event) => {
    if (shouldIgnore(event) || event.button === 2) return;
    event.preventDefault();
    canvas.setPointerCapture(event.pointerId);
    const erasing = state.tool === "eraser" || (event.pointerType === "pen" && (event.button === 5 || (event.buttons & 32)));
    drawing = erasing ? { pointerId: event.pointerId, eraser: true } : {
      pointerId: event.pointerId, eraser: false,
      stroke: { tool: state.tool, color: state.tool === "highlight" ? "#e6ba34" : state.color, width: state.penWidth, points: [point(event)] }
    };
    if (erasing) {
      const p = point(event); eraseAt(doc, pageNo, p[0] * canvas.clientWidth, p[1] * canvas.clientHeight);
    }
  });
  canvas.addEventListener("pointermove", (event) => {
    if (!drawing || event.pointerId !== drawing.pointerId) return;
    if (event.pointerType === "pen") state.lastPenAt = Date.now();
    event.preventDefault();
    const events = typeof event.getCoalescedEvents === "function" ? event.getCoalescedEvents() : [event];
    for (const e of events.length ? events : [event]) {
      const p = point(e);
      if (drawing.eraser) eraseAt(doc, pageNo, p[0] * canvas.clientWidth, p[1] * canvas.clientHeight);
      else drawing.stroke.points.push(p);
    }
    if (!drawing.eraser) paintInk(doc, pageNo, drawing.stroke);
  });
  function finish(event) {
    if (!drawing || event.pointerId !== drawing.pointerId) return;
    if (!drawing.eraser) {
      const stroke = drawing.stroke;
      stroke.points.push(point(event));
      (doc.ink.pages[pageNo] ||= []).push(stroke);
      doc.undo.push({ kind: "add", pageNo, stroke });
      doc.redo.length = 0;
      markDirty(doc);
      paintInk(doc, pageNo);
    }
    drawing = null;
    try { canvas.releasePointerCapture(event.pointerId); } catch (_) {}
  }
  canvas.addEventListener("pointerup", finish);
  canvas.addEventListener("pointercancel", finish);
}
function markDirty(doc) {
  doc.dirty = true; doc.revision++;
  renderTabs(); updateStatus();
  clearTimeout(doc.saveTimer);
  doc.saveTimer = setTimeout(() => { void saveInk(doc); }, 750);
}
async function saveInk(doc) {
  if (!doc.dirty) return;
  if (!doc.path || !api) {
    doc.dirty = false;
    renderTabs(); updateStatus();
    return;
  }
  const revision = doc.revision;
  try {
    await api.saveInk(doc.path, doc.ink);
    if (doc.revision === revision) doc.dirty = false;
    else { clearTimeout(doc.saveTimer); doc.saveTimer = setTimeout(() => void saveInk(doc), 500); }
    renderTabs(); updateStatus();
  } catch (error) { toast("批注保存失败：" + error.message, true); }
}
function historyStep(direction) {
  const doc = currentDoc();
  if (!doc) return;
  const source = direction === -1 ? doc.undo : doc.redo;
  const target = direction === -1 ? doc.redo : doc.undo;
  const action = source.pop();
  if (!action) return;
  const strokes = (doc.ink.pages[action.pageNo] ||= []);
  const shouldAdd = (action.kind === "add" && direction === 1) || (action.kind === "remove" && direction === -1);
  if (shouldAdd) strokes.splice(action.index ?? strokes.length, 0, action.stroke);
  else {
    const index = strokes.indexOf(action.stroke);
    if (index >= 0) strokes.splice(index, 1);
  }
  target.push(action); markDirty(doc); paintInk(doc, action.pageNo);
}
async function action(name) {
  const doc = currentDoc();
  switch (name) {
    case "open": await openDialog(); break;
    case "export":
      if (!doc) return toast("请先打开 PDF");
      if (!doc.path || !api) return toast("请先通过文件选择器打开本地 PDF");
      try { if (await api.exportInk(doc.path, doc.ink)) toast("批注 JSON 已导出"); }
      catch (error) { toast("导出失败：" + error.message, true); }
      break;
    case "sidebar": setSidebar(!state.sidebar); break;
    case "fitWidth": changeZoom("width"); break;
    case "fitPage": changeZoom("page"); break;
    case "zoomIn": if (doc) changeZoom(doc.scale * 1.15); break;
    case "zoomOut": if (doc) changeZoom(doc.scale / 1.15); break;
    case "prev": if (doc) goToPage(doc.current - 1); break;
    case "next": if (doc) goToPage(doc.current + 1); break;
    case "undo": historyStep(-1); break;
    case "redo": historyStep(1); break;
    case "search": if (doc) { $("searchBar").classList.remove("hidden"); $("searchInput").focus(); } break;
    case "closeSearch": $("searchBar").classList.add("hidden"); $("searchInput").value = ""; $("searchCount").textContent = ""; state.searchToken++; break;
    case "searchPrev": searchMove(-1); break;
    case "searchNext": searchMove(1); break;
  }
}
document.addEventListener("click", async (event) => {
  const close = event.target.closest("[data-close]");
  if (close) { event.stopPropagation(); await closeDoc(Number(close.dataset.close)); return; }
  const tab = event.target.closest("[data-doc]");
  if (tab) { await activate(Number(tab.dataset.doc)); return; }
  const tool = event.target.closest("[data-tool]");
  if (tool) { setTool(tool.dataset.tool); return; }
  const color = event.target.closest("[data-color]");
  if (color) {
    state.color = color.dataset.color;
    document.querySelectorAll("[data-color]").forEach((el) => el.classList.toggle("selected", el === color));
    if (state.tool === "hand") setTool("pen");
    return;
  }
  const side = event.target.closest("[data-side]");
  if (side) {
    state.sideMode = side.dataset.side;
    document.querySelectorAll("[data-side]").forEach((el) => el.classList.toggle("active", el === side));
    if (currentDoc()) await renderSidebar(currentDoc());
    return;
  }
  const thumb = event.target.closest(".thumb-entry");
  if (thumb) { goToPage(Number(thumb.dataset.page)); return; }
  const button = event.target.closest("[data-action]");
  if (button) await action(button.dataset.action);
});
$("penSize").addEventListener("input", (event) => {
  state.penWidth = Number(event.target.value);
  $("penSizeValue").textContent = state.penWidth;
});
$("pageNumber").addEventListener("change", (event) => goToPage(Number(event.target.value)));
$("zoomRange").addEventListener("input", (event) => changeZoom(Number(event.target.value) / 100));
$("searchInput").addEventListener("keydown", (event) => {
  if (event.key === "Enter") { event.preventDefault(); void runSearch(); }
});
$("fallbackFile").addEventListener("change", async (event) => {
  for (const file of event.target.files) await openPdf({ name: file.name, bytes: await file.arrayBuffer() });
  event.target.value = "";
});
document.addEventListener("keydown", (event) => {
  const mod = event.ctrlKey || event.metaKey;
  const key = event.key.toLowerCase();
  if (mod && ["o", "f", "z", "y", "s", "p", "+", "=", "-", "0"].includes(key)) {
    event.preventDefault();
    if (key === "o") void openDialog();
    else if (key === "f") void action("search");
    else if (key === "z") historyStep(event.shiftKey ? 1 : -1);
    else if (key === "y") historyStep(1);
    else if (key === "s") { const doc = currentDoc(); if (doc) void saveInk(doc); }
    else if (key === "+" || key === "=") void action("zoomIn");
    else if (key === "-") void action("zoomOut");
    else if (key === "0") changeZoom("width");
    return;
  }
  if (event.key === "Escape") void action("closeSearch");
  if (event.target.matches("input, textarea")) return;
  if (event.key === "PageDown") { event.preventDefault(); void action("next"); }
  if (event.key === "PageUp") { event.preventDefault(); void action("prev"); }
});
let dragDepth = 0;
$("documentArea").addEventListener("dragenter", (event) => {
  event.preventDefault(); dragDepth++; $("dropOverlay").classList.remove("hidden");
});
$("documentArea").addEventListener("dragover", (event) => { event.preventDefault(); event.dataTransfer.dropEffect = "copy"; });
$("documentArea").addEventListener("dragleave", (event) => {
  event.preventDefault(); dragDepth = Math.max(0, dragDepth - 1);
  if (!dragDepth) $("dropOverlay").classList.add("hidden");
});
$("documentArea").addEventListener("drop", async (event) => {
  event.preventDefault(); dragDepth = 0; $("dropOverlay").classList.add("hidden");
  for (const file of event.dataTransfer.files) {
    if (!file.name.toLowerCase().endsWith(".pdf")) continue;
    try {
      const path = api?.getPathForFile(file);
      if (path && api) await openPdf(await api.loadPath(path));
      else await openPdf({ name: file.name, bytes: await file.arrayBuffer() });
    } catch (error) { toast("打开拖入文件失败：" + error.message, true); }
  }
});
let resizeTimer;
new ResizeObserver(() => {
  clearTimeout(resizeTimer);
  resizeTimer = setTimeout(() => {
    const doc = currentDoc();
    if (doc && doc.fit !== "manual") changeZoom(doc.fit);
  }, 200);
}).observe($("documentArea"));
window.addEventListener("beforeunload", () => {
  const doc = currentDoc();
  if (doc?.dirty) void saveInk(doc);
});
setTool("hand");
setSidebar(true);
updateStatus();
