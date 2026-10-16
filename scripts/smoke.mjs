import { _electron as electron } from "playwright-core";
import { Buffer } from "node:buffer";

function createPdf() {
  const stream = "BT /F1 24 Tf 72 700 Td (Hello PDF) Tj ET\n";
  const objects = [
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
    "<< /Length " + Buffer.byteLength(stream) + " >>\nstream\n" + stream + "endstream",
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
  ];
  let pdf = "%PDF-1.4\n";
  const offsets = [0];
  for (let i = 0; i < objects.length; i++) {
    offsets.push(Buffer.byteLength(pdf));
    pdf += (i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
  }
  const xref = Buffer.byteLength(pdf);
  pdf += "xref\n0 " + (objects.length + 1) + "\n0000000000 65535 f \n";
  for (const offset of offsets.slice(1)) pdf += String(offset).padStart(10, "0") + " 00000 n \n";
  pdf += "trailer\n<< /Size " + (objects.length + 1) + " /Root 1 0 R >>\nstartxref\n" + xref + "\n%%EOF\n";
  return Buffer.from(pdf, "ascii");
}
let app;
try {
  app = await electron.launch({ args: ["."], timeout: 60000 });
  const page = await app.firstWindow();
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.locator(".brand").waitFor({ timeout: 30000 });
  await page.locator("#fallbackFile").setInputFiles({
    name: "smoke.pdf", mimeType: "application/pdf", buffer: createPdf()
  });
  await page.locator(".page").first().waitFor({ timeout: 30000 });
  await page.waitForFunction(() => {
    const canvas = document.querySelector(".pdf-layer");
    const loading = document.querySelector(".page-loading");
    return canvas && canvas.width > 0 && loading?.classList.contains("hidden");
  }, null, { timeout: 60000 });
  const text = await page.locator(".textLayer").first().innerText();
  if (!text.includes("Hello PDF")) throw new Error("PDF text layer missing text: " + text);
  await page.locator('[data-tool="pen"]').click();
  const ink = page.locator(".ink-layer").first();
  const box = await ink.boundingBox();
  if (!box) throw new Error("Ink canvas not visible");
  await page.mouse.move(box.x + 50, box.y + 50);
  await page.mouse.down();
  await page.mouse.move(box.x + 120, box.y + 90, { steps: 12 });
  await page.mouse.up();
  const inkPixels = await page.evaluate(() => {
    const canvas = document.querySelector(".ink-layer");
    const pixels = canvas.getContext("2d").getImageData(0, 0, canvas.width, canvas.height).data;
    for (let i = 3; i < pixels.length; i += 4) if (pixels[i] > 0) return true;
    return false;
  });
  if (!inkPixels) throw new Error("Pen stroke not painted");
  if (errors.length) throw new Error("Renderer errors: " + errors.join("; "));
  console.log("Smoke test passed: PDF render, text layer and pointer ink");
} finally {
  if (app) await app.close();
}
