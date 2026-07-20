import { desc } from "drizzle-orm";
import { getDb } from "../../../db";
import { downloadCounts } from "../../../db/schema";

export async function GET() {
  try {
    const rows = await getDb().select().from(downloadCounts).orderBy(desc(downloadCounts.count));
    return Response.json({ downloads: rows, generatedAt: new Date().toISOString() });
  } catch (error) {
    return Response.json({ error: error instanceof Error ? error.message : "Stats unavailable" }, { status: 503 });
  }
}
