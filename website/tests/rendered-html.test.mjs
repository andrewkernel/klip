import assert from "node:assert/strict";
import { access, readFile, stat } from "node:fs/promises";
import test from "node:test";

const root = new URL("../", import.meta.url);

async function render() {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("test", `${process.pid}-${Date.now()}`);
  const { default: worker } = await import(workerUrl.href);
  return worker.fetch(
    new Request("https://klip.example/", { headers: { accept: "text/html" } }),
    { ASSETS: { fetch: async () => new Response("Not found", { status: 404 }) } },
    { waitUntil() {}, passThroughOnException() {} },
  );
}

test("renders the Klip 3.0.1 landing page", async () => {
  const response = await render();
  assert.equal(response.status, 200);
  assert.match(response.headers.get("content-type") ?? "", /^text\/html\b/i);

  const html = await response.text();
  assert.match(html, /<title>Klip[^<]*Low-latency game clipping<\/title>/i);
  assert.match(html, /low-latency game clipping/i);
  assert.match(html, /v3\.0\.1/);
  assert.match(html, /\/api\/download\?artifact=installer/);
  assert.match(html, /\/api\/download\?artifact=portable/);
  assert.doesNotMatch(html, /codex-preview|Your site is taking shape|react-loading-skeleton/i);
});

test("ships both 3.0.1 Windows downloads and checksums", async () => {
  const filenames = [
    "Klip-3.0.1-win64-setup.exe",
    "Klip-3.0.1-win64-portable.zip",
    "SHA256SUMS.txt",
  ];
  await Promise.all(filenames.map((name) => access(new URL(`../public/${name}`, import.meta.url))));

  const [installer, portable, checksums] = await Promise.all([
    stat(new URL("../public/Klip-3.0.1-win64-setup.exe", import.meta.url)),
    stat(new URL("../public/Klip-3.0.1-win64-portable.zip", import.meta.url)),
    readFile(new URL("../public/SHA256SUMS.txt", import.meta.url), "utf8"),
  ]);
  assert.ok(installer.size > 8_000_000);
  assert.ok(portable.size > 8_000_000);
  assert.match(checksums, /Klip-3\.0\.1-win64-setup\.exe/);
  assert.match(checksums, /Klip-3\.0\.1-win64-portable\.zip/);
});

test("download route targets the published 3.0.1 files", async () => {
  const route = await readFile(new URL("../app/api/download/route.ts", import.meta.url), "utf8");
  assert.match(route, /Klip-3\.0\.1-win64-setup\.exe/);
  assert.match(route, /Klip-3\.0\.1-win64-portable\.zip/);
  assert.doesNotMatch(route, /Klip-0\.3\.0/);
});
