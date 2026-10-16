"use strict";
const { contextBridge, ipcRenderer, webUtils } = require("electron");
contextBridge.exposeInMainWorld("viewerAPI", Object.freeze({
  pickPdf: () => ipcRenderer.invoke("pdf:pick"),
  loadPath: (filePath) => ipcRenderer.invoke("pdf:loadPath", filePath),
  getPathForFile: (file) => webUtils.getPathForFile(file),
  loadInk: (filePath) => ipcRenderer.invoke("ink:load", filePath),
  saveInk: (filePath, data) => ipcRenderer.invoke("ink:save", filePath, data),
  exportInk: (filePath, data) => ipcRenderer.invoke("ink:export", filePath, data)
}));
