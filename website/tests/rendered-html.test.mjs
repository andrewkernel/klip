import assert from "node:assert/strict";
import { access, readFile, stat } from "node:fs/promises";
import test from "node:test";
import { createHash, createPublicKey, verify } from "node:crypto";

async function render(path = "/") {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("test", `${process.pid}-${Date.now()}`);
  const { default: worker } = await import(workerUrl.href);
  return worker.fetch(
    new Request(`https://klip.example${path}`, { headers: { accept: "text/html" } }),
    { ASSETS: { fetch: async () => new Response("Not found", { status: 404 }) } },
    { waitUntil() {}, passThroughOnException() {} },
  );
}

test("renders the Klip 2.0.2 beta landing page", async () => {
  const response = await render();
  assert.equal(response.status, 200);
  assert.match(response.headers.get("content-type") ?? "", /^text\/html\b/i);

  const html = await response.text();
  assert.match(html, /<title>Klip[^<]*Low-latency game clipping<\/title>/i);
  assert.match(html, /low-latency game clipping/i);
  assert.match(html, /v2\.0\.2/);
  assert.match(html, /public beta/);
  assert.match(html, /OBS-powered capture/);
  assert.match(html, /duration-first/);
  assert.match(html, /keyframe-aligned/);
  assert.match(html, /in-app updates/);
  assert.match(html, /Linux downloads are not available yet/);
  assert.doesNotMatch(html, /v3\.0\.3/);
  assert.match(html, /https:\/\/github\.com\/andrewkernel\/klip/);
  assert.match(html, /\/api\/download\?artifact=installer/);
  assert.match(html, /\/api\/download\?artifact=portable/);
  assert.doesNotMatch(html, /codex-preview|Your site is taking shape|react-loading-skeleton/i);
});

test("ships both 2.0.2 Windows downloads and checksums", async () => {
  const filenames = [
    "Klip-2.0.2-win64-setup.exe",
    "Klip-2.0.2-win64-portable.zip",
    "SHA256SUMS.txt",
  ];
  await Promise.all(filenames.map((name) => access(new URL(`../public/${name}`, import.meta.url))));

  const [installer, portable, checksums] = await Promise.all([
    stat(new URL("../public/Klip-2.0.2-win64-setup.exe", import.meta.url)),
    stat(new URL("../public/Klip-2.0.2-win64-portable.zip", import.meta.url)),
    readFile(new URL("../public/SHA256SUMS.txt", import.meta.url), "utf8"),
  ]);
  assert.ok(installer.size > 8_000_000);
  assert.ok(portable.size > 8_000_000);
  assert.match(checksums, /Klip-2\.0\.2-win64-setup\.exe/);
  assert.match(checksums, /Klip-2\.0\.2-win64-portable\.zip/);
  for (const name of filenames.slice(0, 2)) {
    const file = await readFile(new URL(`../public/${name}`, import.meta.url));
    const hash = createHash("sha256").update(file).digest("hex");
    assert.ok(checksums.includes(`${hash}  ${name}`), `${name}: checksum mismatch`);
  }
});

test("download route targets the published 2.0.2 files", async () => {
  const route = await readFile(new URL("../app/api/download/route.ts", import.meta.url), "utf8");
  assert.match(route, /Klip-2\.0\.2-win64-setup\.exe/);
  assert.match(route, /Klip-2\.0\.2-win64-portable\.zip/);
  assert.doesNotMatch(route, /Klip-0\.3\.0/);
});

test("update manifest matches the installer and the app's public signing key", async () => {
  const text = await readFile(new URL("../public/updates/windows.manifest", import.meta.url), "utf8");
  assert.ok(!text.includes("\r"));
  const lines = text.split("\n");
  assert.equal(lines.length, 8);
  assert.equal(lines[7], "");
  assert.equal(lines[0], "klip-update-v1");
  assert.equal(lines[1], "2.0.2");
  assert.equal(lines[2], "https://klip-ten-zeta.vercel.app/Klip-2.0.2-win64-setup.exe");
  const installer = await readFile(new URL("../public/Klip-2.0.2-win64-setup.exe", import.meta.url));
  assert.equal(lines[3], createHash("sha256").update(installer).digest("hex"));
  assert.equal(Number(lines[4]), installer.length);
  const now = Math.floor(Date.now() / 1000);
  assert.ok(Number(lines[5]) > now && Number(lines[5]) <= now + 22 * 86400);
  assert.match(lines[6], /^[a-f0-9]{128}$/);
  const header = await readFile(new URL("../../include/klip/update/public_key.h", import.meta.url), "utf8");
  const blob = Buffer.from([...header.matchAll(/"([a-f0-9]+)"/g)].map((match) => match[1]).join(""), "hex");
  assert.equal(blob.length, 72);
  const key = createPublicKey({ format: "jwk", key: {
    kty: "EC", crv: "P-256",
    x: blob.subarray(8, 40).toString("base64url"),
    y: blob.subarray(40, 72).toString("base64url"),
  } });
  assert.ok(verify("sha256", Buffer.from(lines.slice(0, 6).join("\n") + "\n"),
    { key, dsaEncoding: "ieee-p1363" }, Buffer.from(lines[6], "hex")));
});

test("unknown and prototype-named downloads are rejected", async () => {
  for (const query of ["", "?artifact=unknown", "?artifact=__proto__", "?artifact=toString"]) {
    const response = await render(`/api/download${query}`);
    assert.equal(response.status, 400);
    assert.equal(response.headers.get("location"), null);
  }
});

test("download counter uses the hosted D1 database", async () => {
  const database = await readFile(new URL("../db/index.ts", import.meta.url), "utf8");
  assert.match(database, /cloudflare:workers/);
  assert.match(database, /drizzle-orm\/d1/);
  assert.match(database, /await import\("cloudflare:workers"\)/);
  assert.match(database, /drizzle\(env\.DB\)/);
  assert.doesNotMatch(database, /database is unavailable/i);
});

test("download counter supports Vercel Redis with atomic increments", async () => {
  const counter = await readFile(new URL("../db/download-counter.ts", import.meta.url), "utf8");
  assert.match(counter, /UPSTASH_REDIS_REST_URL/);
  assert.match(counter, /UPSTASH_REDIS_REST_TOKEN/);
  assert.match(counter, /\["INCR", countKey\(artifact\)\]/);
  assert.match(counter, /\["MGET", \.\.\.keys\]/);
});

test("download statistics expose an uncached total", async () => {
  const route = await readFile(new URL("../app/api/downloads/route.ts", import.meta.url), "utf8");
  assert.match(route, /const total = rows\.reduce/);
  assert.match(route, /\{ total, downloads: rows/);
  assert.match(route, /Cache-Control.*no-store/);
});
