import { drizzle } from "drizzle-orm/d1";

export async function getDb() {
  const { env } = await import("cloudflare:workers");
  return drizzle(env.DB);
}
