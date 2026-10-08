import { incrementDownload } from "../../../db/download-counter";

const targets = {
  installer: "/Klip-2.0.0-win64-setup.exe",
  portable: "/Klip-2.0.0-win64-portable.zip",
} as const;

export async function GET(request: Request) {
  const artifact = new URL(request.url).searchParams.get("artifact");
  if (artifact !== "installer" && artifact !== "portable") {
    return Response.json({ error: "Unknown download artifact" }, { status: 400 });
  }
  const destination = targets[artifact];
  try {
    await incrementDownload(artifact);
  } catch (error) {
    console.error("download counter unavailable", error);
  }
  return Response.redirect(new URL(destination, request.url), 302);
}
