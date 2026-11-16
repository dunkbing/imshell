// the search index: every docs page split at its ## and ### headings, as plain text. the page's
// script (components/DocSearch.astro) fetches it once and filters in the browser
import { docPages } from "./_docs";

const raw = import.meta.glob<string>("../../../../docs/*.md", { query: "?raw", import: "default", eager: true });

// github's heading ids, which the rendered page uses
const slug = (heading: string) =>
  heading.toLowerCase().replace(/`/g, "").replace(/[^\w\s-]/g, "").trim().replace(/\s/g, "-");

const plain = (text: string) =>
  text
    .replace(/\[([^\]]+)\]\([^)]*\)/g, "$1")
    .replace(/[`*]/g, "")
    .replace(/\s+/g, " ")
    .trim();

export function GET() {
  const entries: { page: string; title: string; heading: string; anchor: string; text: string }[] = [];
  for (const page of docPages) {
    let heading = "";
    let lines: string[] = [];
    let fenced = false;
    const flush = () => {
      const text = plain(lines.join("\n"));
      if (heading || text)
        entries.push({ page: page.slug, title: page.title, heading: plain(heading), anchor: heading ? slug(heading) : "", text });
      lines = [];
    };
    for (const line of (raw[`../../../../docs/${page.slug}.md`] ?? "").split("\n")) {
      if (line.startsWith("```")) fenced = !fenced;
      const h = !fenced && line.match(/^#{2,3} (.+)/);
      if (h) {
        flush();
        heading = h[1];
      } else if (!/^# /.test(line) || fenced) {
        lines.push(line);
      }
    }
    flush();
  }
  return new Response(JSON.stringify(entries), { headers: { "Content-Type": "application/json" } });
}
