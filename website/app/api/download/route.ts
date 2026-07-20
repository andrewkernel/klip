import { sql } from "drizzle-orm";
import { getDb } from "../../../db";
import { downloadCounts } from "../../../db/schema";

const targets = {
  installer: "/Klip-3.0.1-win64-setup.exe",
  portable: "/Klip-3.0.1-win64-portable.zip",
} as const;

export async function GET(request: Request) {
  const artifact = new URL(request.url).searchParams.get("artifact") as keyof typeof targets | null;
  const destination = artifact ? targets[artifact] : undefined;
  if (!destination) return Response.json({ error: "Unknown download artifact" }, { status: 400 });
  try {
    await getDb().insert(downloadCounts).values({ artifact, count: 1 }).onConflictDoUpdate({
      target: downloadCounts.artifact,
      set: { count: sql`${downloadCounts.count} + 1`, updatedAt: sql`CURRENT_TIMESTAMP` },
    });
  } catch (error) {
    console.error("download counter unavailable", error);
  }
  return Response.redirect(new URL(destination, request.url), 302);
}
