import type { Metadata } from "next";
import { headers } from "next/headers";
import "./globals.css";

export async function generateMetadata(): Promise<Metadata> {
  const requestHeaders = await headers();
  const host =
    requestHeaders.get("x-forwarded-host") ??
    requestHeaders.get("host") ??
    "localhost:3000";
  const protocol =
    requestHeaders.get("x-forwarded-proto") ??
    (host.startsWith("localhost") ? "http" : "https");
  const metadataBase = new URL(`${protocol}://${host}`);

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
