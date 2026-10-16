import { cp, mkdir } from "node:fs/promises";
import { resolve } from "node:path";
const source = resolve("node_modules/pdfjs-dist");
const destination = resolve("dist");
await mkdir(destination, { recursive: true });
for (const directory of ["cmaps", "standard_fonts", "wasm"]) {
  try {
    await cp(resolve(source, directory), resolve(destination, directory), { recursive: true });
    console.log("Bundled PDF.js", directory);
  } catch (error) {
    if (error.code !== "ENOENT") throw error;
    console.log("Optional PDF.js assets absent:", directory);
  }
}
