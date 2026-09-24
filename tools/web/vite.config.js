import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Web Bluetooth needs a secure context: http://localhost is one, a LAN IP is not.
export default defineConfig({ plugins: [react()], server: { port: 5173, open: true } });
