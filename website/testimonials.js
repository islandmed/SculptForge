// "What artists say": quotes from testimonials.json (consent on record, the artist's exact wording; never edited).
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
      const now = [...list.querySelectorAll("blockquote")].map((b) => b.textContent.trim());
      if (now.length === shown.length && shown.every((t, i) => now[i] === t.quote.trim())) continue;
      list.replaceChildren(...shown.map((t) => {
        const fig = document.createElement("figure"), q = document.createElement("blockquote"),
              p = document.createElement("p"), cap = document.createElement("figcaption"),
              name = document.createElement("span");
        fig.className = "voice";
        p.textContent = t.quote;
        q.append(p);
        name.className = "voice-name";
        name.textContent = t.name;
        cap.append(name);
        if (typeof t.role === "string" && t.role) {
          const role = document.createElement("span");
          role.className = "voice-role";
          role.textContent = t.role;
          cap.append(role);
        }
        fig.append(q, cap);
        return fig;
      }));
      list.classList.toggle("voices-many", shown.length > 1);
    }
  }).catch(() => {});
})();
