// the docs are the repo's own docs/*.md, rendered as they are. docs/README.md's link list
// ("- [Title](page.md): what it covers") gives the order, titles and descriptions, so a new
// page is a markdown file plus one line there. the underscore keeps this from being a route
import readme from "../../../../docs/README.md?raw";

export interface DocPage {
  slug: string;
  title: string;
  description: string;
}

interface DocModule {
  Content: any;
}

const modules = import.meta.glob<DocModule>("../../../../docs/*.md", { eager: true });

export const docPages: DocPage[] = [...readme.matchAll(/^- \[([^\]]+)\]\(([\w-]+)\.md\): (.+)$/gm)].map(
  ([, title, slug, description]) => ({ slug, title, description: description.replaceAll("`", "") }),
);

export function docContent(slug: string) {
  return modules[`../../../../docs/${slug}.md`]?.Content;
}
