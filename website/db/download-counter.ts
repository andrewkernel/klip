import { sql } from "drizzle-orm";
import { getDb } from ".";
import { downloadCounts } from "./schema";

export type DownloadArtifact = "installer" | "portable";

type DownloadCount = {
  artifact: DownloadArtifact;
  count: number;
  updatedAt: string | null;
};

const artifacts: DownloadArtifact[] = ["installer", "portable"];
const countKey = (artifact: DownloadArtifact) => `klip:downloads:${artifact}`;
const updatedKey = (artifact: DownloadArtifact) => `klip:downloads:${artifact}:updated-at`;

function redisConfiguration() {
  const url = process.env.UPSTASH_REDIS_REST_URL ?? process.env.KV_REST_API_URL;
  const token = process.env.UPSTASH_REDIS_REST_TOKEN ?? process.env.KV_REST_API_TOKEN;
  return url && token ? { url: url.replace(/\/$/, ""), token } : null;
}

async function redisCommand(command: Array<string | number>) {
  const configuration = redisConfiguration();
  if (!configuration) return null;

  const response = await fetch(configuration.url, {
    method: "POST",
    headers: {
      Authorization: `Bearer ${configuration.token}`,
      "Content-Type": "application/json",
    },
    body: JSON.stringify(command),
    cache: "no-store",
  });
  if (!response.ok) throw new Error(`Download counter storage returned ${response.status}`);
  const payload = (await response.json()) as { result?: unknown; error?: string };
  if (payload.error) throw new Error(payload.error);
  return payload.result;
}

export async function incrementDownload(artifact: DownloadArtifact) {
  if (redisConfiguration()) {
    const updatedAt = new Date().toISOString();
    await Promise.all([
      redisCommand(["INCR", countKey(artifact)]),
      redisCommand(["SET", updatedKey(artifact), updatedAt]),
    ]);
    return;
  }

  const db = await getDb();
  await db.insert(downloadCounts).values({ artifact, count: 1 }).onConflictDoUpdate({
    target: downloadCounts.artifact,
    set: { count: sql`${downloadCounts.count} + 1`, updatedAt: sql`CURRENT_TIMESTAMP` },
  });
}

export async function listDownloads(): Promise<DownloadCount[]> {
  if (redisConfiguration()) {
    const keys = artifacts.flatMap((artifact) => [countKey(artifact), updatedKey(artifact)]);
    const values = (await redisCommand(["MGET", ...keys])) as Array<string | null>;
    return artifacts.map((artifact, index) => ({
      artifact,
      count: Number(values[index * 2] ?? 0),
      updatedAt: values[index * 2 + 1] ?? null,
    })).sort((left, right) => right.count - left.count);
  }

  const db = await getDb();
  const rows = await db.select().from(downloadCounts);
  return rows
    .map((row) => ({ ...row, artifact: row.artifact as DownloadArtifact }))
    .sort((left, right) => right.count - left.count);
}
