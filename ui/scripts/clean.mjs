import { rm } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const uiRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
await rm(path.resolve(uiRoot, "../app/assets/ui/browser"), { recursive: true, force: true });
await rm(path.resolve(uiRoot, "../app/assets/ui/ui-manifest.json"), { force: true });