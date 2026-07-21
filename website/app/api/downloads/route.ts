import { listDownloads } from "../../../db/download-counter";

export async function GET() {
  try {
    const rows = await listDownloads();
    const total = rows.reduce((sum, row) => sum + row.count, 0);
    return Response.json(
      { total, downloads: rows, generatedAt: new Date().toISOString() },
      { headers: { "Cache-Control": "no-store" } },
    );
  } catch (error) {
    return Response.json({ error: error instanceof Error ? error.message : "Stats unavailable" }, { status: 503 });
  }
}
