import { defineConfig } from "vite";
export default defineConfig({
  base: "./",
  build: { target: "chrome128", outDir: "dist", assetsInlineLimit: 0 },
  server: { host: "127.0.0.1", port: 5173, strictPort: true }
});
