export function getDb() {
  // The production Vercel build does not provide Cloudflare's `env` binding.
  // Keep the counter routes optional so the main site can render on every host.
  throw new Error("Download counter database is unavailable in this deployment");
}
