// "What artists say": quotes from testimonials.json (consent on record, the artist's exact wording; never edited).
// An optional "context" line (ours, <= 90 chars) says which app feature a quote is about; never anything about the person.
// The pages carry the same quotes as static markup for crawlers and no-JS; this only redraws the list when the JSON
// holds something the markup does not (a new quote landed in the JSON first).
(() => {
  const lists = document.querySelectorAll("[data-testimonials]");
  if (!lists.length) return;
  fetch("testimonials.json", { cache: "no-cache" }).then((r) => (r.ok ? r.json() : null)).then((all) => {
    if (!Array.isArray(all)) return;
    const ok = all.filter((t) => t && typeof t.quote === "string" && t.quote && typeof t.name === "string" && t.name);
    if (!ok.length) return;
    for (const list of lists) {
      const max = Number(list.dataset.testimonials) || ok.length, shown = ok.slice(0, max);
      const ctx = (t) => (typeof t.context === "string" ? t.context.trim().slice(0, 90) : "");
      const now = [...list.querySelectorAll(".voice")].map((f) => [f.querySelector("blockquote")?.textContent.trim(),
        f.querySelector(".voice-context")?.textContent.trim() || ""]);
      if (now.length === shown.length && shown.every((t, i) => now[i][0] === t.quote.trim() && now[i][1] === ctx(t))) continue;
      list.replaceChildren(...shown.map((t) => {
        const fig = document.createElement("figure"), q = document.createElement("blockquote"),
              p = document.createElement("p"), cap = document.createElement("figcaption"),
              name = document.createElement("span");
        fig.className = "voice";
        p.textContent = t.quote;
        q.append(p);
        fig.append(q);
        if (ctx(t)) {
          const c = document.createElement("p");
          c.className = "voice-context";
          c.textContent = ctx(t);
          fig.append(c);
        }
        name.className = "voice-name";
        name.textContent = t.name;
        cap.append(name);
        if (typeof t.role === "string" && t.role) {
          const role = document.createElement("span");
          role.className = "voice-role";
          role.textContent = t.role;
          cap.append(role);
        }
        fig.append(cap);
        return fig;
      }));
      list.classList.toggle("voices-many", shown.length > 1);
    }
  }).catch(() => {});
})();
