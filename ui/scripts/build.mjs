import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { copyFile, mkdir, readdir, readFile, rm, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { build } from "vite";

const uiRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const outputRoot = path.resolve(uiRoot, "../app/assets/ui/browser");
const manifestPath = path.resolve(uiRoot, "../app/assets/ui/ui-manifest.json");
const sourceRoot = path.resolve(uiRoot, "src");
const splashLogoSource = path.resolve(uiRoot, "src/assets/studio-logo.png");
const splashLogoOutput = path.resolve(outputRoot, "assets/studio-logo.png");

function sha256(data) {
  return createHash("sha256").update(data).digest("hex");
}

async function filesUnder(directory) {
  const entries = await readdir(directory, { withFileTypes: true });
  const files = await Promise.all(entries.map(async (entry) => {
    const entryPath = path.join(directory, entry.name);
    return entry.isDirectory() ? filesUnder(entryPath) : [entryPath];
  }));
  return files.flat();
}

function sourceRevision() {
  try {
    return execFileSync("git", ["-C", uiRoot, "rev-parse", "HEAD"], { encoding: "utf8" }).trim();
  } catch {
    return "unavailable";
  }
}

await rm(outputRoot, { recursive: true, force: true });
await build({ configFile: path.join(uiRoot, "vite.config.js") });
await mkdir(path.dirname(splashLogoOutput), { recursive: true });
await copyFile(splashLogoSource, splashLogoOutput);

const sourceFiles = (await filesUnder(sourceRoot)).concat([
  path.join(uiRoot, "index.html"),
  path.join(uiRoot, "package-lock.json"),
  path.join(uiRoot, "package.json"),
  path.join(uiRoot, "postcss.config.js"),
  path.join(uiRoot, "tailwind.config.js"),
  path.join(uiRoot, "vite.config.js")
]).sort();
const sourceDigest = createHash("sha256");
for (const sourceFile of sourceFiles) {
  sourceDigest.update(path.relative(uiRoot, sourceFile).replaceAll("\\", "/"));
  sourceDigest.update(await readFile(sourceFile));
}

const emittedFiles = await filesUnder(outputRoot);
const assets = {};
for (const emittedFile of emittedFiles.sort()) {
  const relativePath = path.relative(path.dirname(manifestPath), emittedFile).replaceAll("\\", "/");
  let content = await readFile(emittedFile);
  if ([".css", ".html", ".js", ".json"].includes(path.extname(emittedFile))) {
    const normalizedContent = Buffer.from(content.toString("utf8").replaceAll("\r\n", "\n"));
    if (!content.equals(normalizedContent)) {
      await writeFile(emittedFile, normalizedContent);
      content = normalizedContent;
    }
  }
  assets[relativePath] = { sha256: sha256(content), bytes: content.byteLength };
}

if (!Object.hasOwn(assets, "browser/index.html")) {
  throw new Error("Vite did not emit browser/index.html.");
}

const manifest = {
  schema_version: 1,
  entry_html: "browser/index.html",
  source_revision: sourceRevision(),
  source_sha256: sourceDigest.digest("hex"),
  assets
};
await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`, "utf8");