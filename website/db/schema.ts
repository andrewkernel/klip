import { integer, sqliteTable, text } from "drizzle-orm/sqlite-core";

export const downloadCounts = sqliteTable("download_counts", {
  artifact: text("artifact").primaryKey(),
  count: integer("count").notNull().default(0),
  updatedAt: text("updated_at").notNull().default("CURRENT_TIMESTAMP"),
});
