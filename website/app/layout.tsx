import type { Metadata } from "next";
import { headers } from "next/headers";
import "./globals.css";

export async function generateMetadata(): Promise<Metadata> {
  const requestHeaders = await headers();
  // Proxies can forward comma-separated values or malformed host headers.
  // Keep metadata generation non-fatal so a custom Vercel domain never turns
  // an otherwise healthy page into a 500 response.
  const forwardedHost = requestHeaders.get("x-forwarded-host")?.split(",")[0]?.trim();
  const requestHost = requestHeaders.get("host")?.split(",")[0]?.trim();
  const host = forwardedHost || requestHost || "localhost:3000";
  const forwardedProtocol = requestHeaders.get("x-forwarded-proto")?.split(",")[0]?.trim();
  const protocol = forwardedProtocol || (host.startsWith("localhost") ? "http" : "https");
  let metadataBase: URL;
  try {
    metadataBase = new URL(`${protocol}://${host}`);
  } catch {
    metadataBase = new URL("https://klip-ten-zeta.vercel.app");
  }

  return {
    metadataBase,
    title: "Klip — Low-latency game clipping",
    description:
      "Save replay clips and record full gaming sessions with focused, local Windows capture.",
    applicationName: "Klip",
    icons: {
      icon: "/favicon.ico",
      shortcut: "/favicon.ico",
    },
    openGraph: {
      type: "website",
      title: "Klip — Low-latency game clipping",
      description: "Capture the moment before you miss it.",
      siteName: "Klip",
      images: [{ url: "/og.png", width: 1200, height: 630, alt: "Klip game clipping for Windows" }],
    },
    twitter: {
      card: "summary_large_image",
      title: "Klip — Low-latency game clipping",
      description: "Capture the moment before you miss it.",
      images: ["/og.png"],
    },
  };
}

export default function RootLayout({ children }: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
