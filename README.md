# PDF Document Viewer

A lightweight, education-focused Windows PDF viewer for math, physics and chemistry textbooks and lecture notes. Uses Mozilla PDF.js for font, formula and vector rendering, and keeps handwriting annotations **separate from the original PDF**.

## Features

- Offline Windows desktop app; open PDFs with a file picker or drag-and-drop.
- High-fidelity PDF.js rendering with bundled CMaps, standard fonts and WASM resources.
- Lazy page rendering, bounded pixel count, cancellable render jobs, page thumbnails, table of contents, and zoom / fit-to-width / fit-to-page.
- Touch and pen input with pressure-sensitive handwriting, highlighter, eraser, palm rejection and undo/redo.
- Office-inspired ribbon, multiple document tabs, text search and copyable text layer.
- Annotation auto-save in the app data folder; export JSON on demand. **The PDF is never overwritten.**
- No print feature, menu item or Ctrl+P shortcut.

## Build

Requirements: Node.js 22, Windows 10/11 x64.

```bash
npm install
npm run dev
npm run dist
```

Installer: `release/PDF-Document-Viewer-Setup-0.1.0.exe`. The GitHub Actions **Build Windows installer** workflow also builds and uploads the installer.

## Notes

PDF.js displays the content according to the source PDF's embedded fonts and resources. No viewer can guarantee correction of an incorrectly generated or corrupt PDF. The app is not a PDF editor; annotations are stored separately in JSON. The installer is unsigned, so Windows SmartScreen may display a warning.

License: MIT (application code). PDF.js and Electron retain their respective third-party licenses.
