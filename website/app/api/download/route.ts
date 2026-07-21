import { incrementDownload } from "../../../db/download-counter";

const targets = {
  installer: "/Klip-3.0.2-win64-setup.exe",
  portable: "/Klip-3.0.2-win64-portable.zip",
} as const;

export async function GET(request: Request) {
  const artifact = new URL(request.url).searchParams.get("artifact") as keyof typeof targets | null;
  const destination = artifact ? targets[artifact] : undefined;
  if (!destination) return Response.json({ error: "Unknown download artifact" }, { status: 400 });
  try {
    await incrementDownload(artifact);
  } catch (error) {
    console.error("download counter unavailable", error);
  }
  return Response.redirect(new URL(destination, request.url), 302);
}
