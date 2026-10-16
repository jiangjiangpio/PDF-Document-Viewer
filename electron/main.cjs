"use strict";
const { app, BrowserWindow, dialog, ipcMain, protocol, net } = require("electron");
const fs = require("node:fs/promises");
const path = require("node:path");
const crypto = require("node:crypto");
const { pathToFileURL } = require("node:url");

protocol.registerSchemesAsPrivileged([
  { scheme: "app", privileges: { standard: true, secure: true, supportFetchAPI: true, stream: true } }
]);
const opened = new Set();
let mainWindow;
function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1440, height: 900, minWidth: 860, minHeight: 600,
    backgroundColor: "#f3f5f9", show: false, autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      contextIsolation: true, nodeIntegration: false, sandbox: false,
      webSecurity: true
    }
  });
  mainWindow.once("ready-to-show", () => mainWindow.show());
  mainWindow.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
  mainWindow.webContents.on("will-navigate", (event) => event.preventDefault());
  mainWindow.webContents.on("before-input-event", (event, input) => {
    if ((input.control || input.meta) && input.key.toLowerCase() === "p") event.preventDefault();
  });
  if (process.env.VITE_DEV_SERVER_URL) mainWindow.loadURL(process.env.VITE_DEV_SERVER_URL);
  else mainWindow.loadURL("app://viewer/index.html");
}
async function loadPdf(filePath) {
  if (typeof filePath !== "string" || path.extname(filePath).toLowerCase() !== ".pdf") throw new Error("请选择 PDF 文件");
  const resolved = path.resolve(filePath);
  const stat = await fs.stat(resolved);
  if (!stat.isFile() || stat.size > 1024 * 1024 * 1024) throw new Error("文件太大");
  const bytes = await fs.readFile(resolved);
  opened.add(resolved);
  return { path: resolved, name: path.basename(resolved), bytes };
}
function inkPath(filePath) {
  const hash = crypto.createHash("sha256").update(path.resolve(filePath).toLowerCase()).digest("hex");
  return path.join(app.getPath("userData"), "annotations", hash + ".json");
}
function checkOpened(filePath) {
  if (!opened.has(path.resolve(filePath))) throw new Error("文件尚未打开");
}
app.whenReady().then(() => {
  const dist = path.resolve(__dirname, "../dist");
  protocol.handle("app", (request) => {
    try {
      const url = new URL(request.url);
      if (url.hostname !== "viewer") return new Response("Not found", { status: 404 });
      const relative = decodeURIComponent(url.pathname).replace(/^\/+/, "") || "index.html";
      const file = path.resolve(dist, relative);
      if (!file.startsWith(dist + path.sep)) return new Response("Not found", { status: 404 });
      return net.fetch(pathToFileURL(file).toString());
    } catch (_) { return new Response("Not found", { status: 404 }); }
  });
  ipcMain.handle("pdf:pick", async () => {
    const result = await dialog.showOpenDialog(mainWindow, {
      properties: ["openFile", "multiSelections"],
      filters: [{ name: "PDF", extensions: ["pdf"] }]
    });
    return result.canceled ? [] : Promise.all(result.filePaths.map(loadPdf));
  });
  ipcMain.handle("pdf:loadPath", (_event, filePath) => loadPdf(filePath));
  ipcMain.handle("ink:load", async (_event, filePath) => {
    checkOpened(filePath);
    try { return JSON.parse(await fs.readFile(inkPath(filePath), "utf8")); }
    catch (error) { if (error.code === "ENOENT") return { version: 1, pages: {} }; throw error; }
  });
  ipcMain.handle("ink:save", async (_event, filePath, data) => {
    checkOpened(filePath);
    if (!data || data.version !== 1 || typeof data.pages !== "object") throw new Error("无效批注");
    const file = inkPath(filePath);
    await fs.mkdir(path.dirname(file), { recursive: true });
    const temporary = file + ".tmp";
    await fs.writeFile(temporary, JSON.stringify(data), "utf8");
    await fs.rename(temporary, file);
    return true;
  });
  ipcMain.handle("ink:export", async (_event, filePath, data) => {
    checkOpened(filePath);
    const result = await dialog.showSaveDialog(mainWindow, {
      defaultPath: path.basename(filePath, ".pdf") + ".annotations.json",
      filters: [{ name: "JSON", extensions: ["json"] }]
    });
    if (result.canceled) return false;
    await fs.writeFile(result.filePath, JSON.stringify(data, null, 2), "utf8");
    return true;
  });
  createWindow();
  app.on("activate", () => { if (BrowserWindow.getAllWindows().length === 0) createWindow(); });
});
app.on("window-all-closed", () => { if (process.platform !== "darwin") app.quit(); });
