import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Ship to view/ERCF/dist/ so Prisma loads ERCF/dist/index.html (same layout as ERAS ui → dist).
export default defineConfig({
  base: "./",
  plugins: [react()],
  build: {
    outDir: "./dist",
    emptyOutDir: true,
  },
});
