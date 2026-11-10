import { defineConfig } from "astro/config";
import tailwindcss from "@tailwindcss/vite";

// a static site: any static host serves dist/. MYDEAR_SITE_BASE is the path it lives under
// ("/mydear" for a github pages project site), MYDEAR_SITE_URL its origin
const base = process.env.MYDEAR_SITE_BASE ?? "";

// the docs are the repo's own docs/*.md: their links between pages ("ui.md#fonts") become
// site routes ("/docs/ui#fonts"), README.md the docs index
function docLinks() {
  const visit = (node: any) => {
    if (node.type === "link" && typeof node.url === "string" && !/^[a-z]+:/i.test(node.url)) {
      const m = node.url.match(/^(?:\.\/)?([\w-]+)\.md(#.*)?$/);
      if (m) node.url = `${base}/docs${m[1] === "README" ? "" : `/${m[1]}`}${m[2] ?? ""}`;
    }
    node.children?.forEach(visit);
  };
  return (tree: any) => {
    visit(tree);
  };
}

export default defineConfig({
  site: process.env.MYDEAR_SITE_URL,
  base: base || undefined,
  trailingSlash: "never",
  markdown: {
    remarkPlugins: [docLinks],
    shikiConfig: { themes: { light: "github-light", dark: "github-dark" } },
  },
  // the docs live one folder up, in the repo's docs/
  vite: { plugins: [tailwindcss()], server: { fs: { allow: [".."] } } },
});
