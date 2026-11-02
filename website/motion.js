// SculptForge website: motion and the user guide's interactions. No frameworks; visits and download clicks are
// counted anonymously with GoatCounter (no cookies, no personal data).
// - Sections, cards, screenshots and videos rise into place as they scroll into view (staggered in rows).
// - A scroll progress bar; page-to-page transitions (style.css, View Transitions API) slide in the direction
//   you're reading.
// - Guide pages: swipe (touch) or the arrow keys for the previous / next chapter, the contents drawer on
//   phones, search, copy buttons on code and a full-size view of screenshots.
// - A site menu button when the header links don't fit.
// Everything respects "reduce motion" and works as plain links without JavaScript.
(() => {
  const still = matchMedia("(prefers-reduced-motion: reduce)").matches;
  const phone = () => matchMedia("(max-width: 620px)").matches;
  const guide = document.body.classList.contains("guide-page");

  // --- Page transitions: tell the next page which way we went (forward / back) ---
  const go = (href, dir) => {
    try { sessionStorage.setItem("sf-vt", dir); } catch (e) { /* private mode */ }
    location.href = href;
  };
  addEventListener("pagereveal", (e) => {
    let dir = "";
    try { dir = sessionStorage.getItem("sf-vt") || ""; sessionStorage.removeItem("sf-vt"); } catch (err) { /* */ }
    if (e.viewTransition && dir) e.viewTransition.types.add(dir);
  });
  document.addEventListener("click", (e) => {
    const a = e.target.closest("a[rel=prev], a[rel=next]");
    if (a && !e.metaKey && !e.ctrlKey && !e.shiftKey && e.button === 0) {
      e.preventDefault();
      go(a.href, a.rel === "prev" ? "back" : "forward");
    }
  });

  // --- Scroll progress (CSS scroll timelines where supported, otherwise this) ---
  const bar = document.querySelector(".scroll-progress");
  if (bar && !CSS.supports("animation-timeline: scroll()")) {
    let queued = false;
    const update = () => {
      queued = false;
      const max = document.documentElement.scrollHeight - innerHeight;
      bar.style.transform = `scaleX(${max > 0 ? Math.min(1, scrollY / max) : 0})`;
      document.body.classList.toggle("scrolled", scrollY > 8);
    };
    addEventListener("scroll", () => { if (!queued) { queued = true; requestAnimationFrame(update); } }, { passive: true });
    update();
  }

  // --- Reveal on scroll ---
  if (!still && "IntersectionObserver" in window) {
    const selector = guide
      ? ".guide-article figure, .guide-article .table-wrap, .guide-article .callout, .guide-article .code-block, .pager a"
      : ".section h2, .section-lede, .card, .demo, .two-col > div, .dl, .faq details, .spec, .vfx-points li, " +
        ".vfx-code, .steps > *, .brush, .platform-strip";
    const items = [...document.querySelectorAll(selector)].filter((el) => {
      const r = el.getBoundingClientRect();
      return r.top > innerHeight * 0.9; // what's already on screen stays put
    });
    const reveal = new IntersectionObserver((entries) => {
      for (const e of entries) {
        if (!e.isIntersecting) continue;
        e.target.classList.add("shown");
        reveal.unobserve(e.target);
      }
    }, { rootMargin: "0px 0px -6% 0px", threshold: 0.08 });
    // Siblings in a row arrive one after another (not in the phone's swipe rows).
    const order = new Map();
    for (const el of items) {
      const n = order.get(el.parentElement) || 0;
      order.set(el.parentElement, n + 1);
      el.style.setProperty("--reveal-delay", `${Math.min(n % 4, 3) * 70}ms`);
      el.classList.add(el.matches("figure, .demo, .vfx-code") ? "reveal-media" : "reveal");
      reveal.observe(el);
    }
  }

  // --- Site menu: when the header links don't fit beside the logo (a narrow window, a tablet or a phone), the
  // secondary links move into a menu behind a button; Download, Discord and GitHub stay in the bar. Measured, not a fixed
  // breakpoint, since pages have different links. ---
  const siteHeader = document.querySelector(".nav");
  const mainNav = siteHeader?.querySelector("nav");
  if (siteHeader && mainNav) {
    const inner = siteHeader.querySelector(".nav-inner"), brand = siteHeader.querySelector(".brand");
    const items = [...mainNav.querySelectorAll("a:not(.gh):not(.nav-dl)")];
    const ghs = [...mainNav.querySelectorAll("a.gh")]; // Discord and GitHub
    if (!brand.hasAttribute("aria-label")) brand.setAttribute("aria-label", brand.textContent.trim());
    const menu = document.createElement("div");
    menu.className = "nav-menu";
    menu.id = "site-menu";
    menu.hidden = true;
    const list = document.createElement("div");
    list.className = "wrap";
    for (const a of items) list.append(a.cloneNode(true));
    for (const gh of ghs) list.append(Object.assign(gh.cloneNode(true), { className: "menu-gh" }));
    menu.append(list);
    siteHeader.append(menu);
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "nav-toggle";
    btn.setAttribute("aria-controls", "site-menu");
    btn.setAttribute("aria-expanded", "false");
    btn.setAttribute("aria-label", "Menu");
    btn.innerHTML = "<span></span><span></span><span></span>";
    mainNav.append(btn);
    const setOpen = (open) => {
      menu.hidden = !open;
      btn.setAttribute("aria-expanded", String(open));
      siteHeader.classList.toggle("menu-open", open);
    };
    btn.addEventListener("click", () => setOpen(menu.hidden));
    menu.addEventListener("click", (e) => { if (e.target.closest("a")) setOpen(false); });
    document.addEventListener("click", (e) => { if (!menu.hidden && !siteHeader.contains(e.target)) setOpen(false); });
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape" && !menu.hidden) { setOpen(false); btn.focus(); }
    });
    // Compact: the secondary links go into the menu (too wide for the bar, or already hidden by the phone layout
    // in style.css). Tight: on the narrowest screens Discord and GitHub go too, so Download and the button fit by the logo.
    // Tiny: below that (the smallest phones) the logo drops its name.
    const tooWide = () => {
      const pad = getComputedStyle(inner);
      const room = inner.clientWidth - parseFloat(pad.paddingLeft) - parseFloat(pad.paddingRight);
      return brand.offsetWidth + mainNav.offsetWidth + 32 > room;
    };
    const fit = () => {
      siteHeader.classList.remove("compact", "tight", "tiny");
      const compact = tooWide() || items.some((a) => a.offsetWidth === 0);
      siteHeader.classList.toggle("compact", compact);
      siteHeader.classList.toggle("tight", compact && tooWide());
      siteHeader.classList.toggle("tiny", siteHeader.classList.contains("tight") && tooWide());
      if (!compact) setOpen(false);
    };
    new ResizeObserver(fit).observe(inner);
    document.fonts?.ready.then(fit);
    fit();
  }

  // --- Download counts: GoatCounter (loaded beside this script) records which file was downloaded, so the
  // stats show downloads per platform next to the visits and the sites that sent them. ---
  document.addEventListener("click", (e) => {
    const a = e.target.closest('a[href*="github.com/islandmed/SculptForge/releases"]');
    if (!a || !window.goatcounter?.count) return;
    const file = a.href.split("/download/")[1] || "releases page";
    const title = a.innerText.replace(/\s+/g, " ").trim().slice(0, 60);
    window.goatcounter.count({ path: "download: " + file, title, event: true });
  });

  if (!guide) return;

  // --- Contents drawer (phones and tablets) ---
  const toggle = document.querySelector(".toc-toggle");
  const panel = document.getElementById("guide-toc");
  if (toggle && panel) {
    toggle.addEventListener("click", () => {
      const open = toggle.getAttribute("aria-expanded") !== "true";
      toggle.setAttribute("aria-expanded", String(open));
      panel.classList.toggle("open", open);
    });
    panel.addEventListener("click", (e) => {
      if (e.target.closest("a") && toggle.getAttribute("aria-expanded") === "true") toggle.click();
    });
  }

  // --- Previous / next chapter: arrow keys and swipes ---
  const prev = document.body.dataset.prev, next = document.body.dataset.next;
  document.addEventListener("keydown", (e) => {
    if (e.altKey || e.ctrlKey || e.metaKey || e.shiftKey || e.target.closest("input, textarea")) return;
    if (e.key === "ArrowLeft" && prev) go(prev, "back");
    if (e.key === "ArrowRight" && next) go(next, "forward");
  });
  const main = document.querySelector(".guide-main");
  if (main) {
    let x0 = 0, y0 = 0, t0 = 0, dx = 0, tracking = false, horizontal = false;
    main.addEventListener("touchstart", (e) => {
      if (e.touches.length !== 1 || e.target.closest(".table-wrap, .code-block, input")) return;
      tracking = true; horizontal = false; dx = 0;
      x0 = e.touches[0].clientX; y0 = e.touches[0].clientY; t0 = performance.now();
      main.style.transition = "none";
    }, { passive: true });
    main.addEventListener("touchmove", (e) => {
      if (!tracking) return;
      dx = e.touches[0].clientX - x0;
      const dy = e.touches[0].clientY - y0;
      if (!horizontal && Math.abs(dy) > 12 && Math.abs(dy) > Math.abs(dx)) { tracking = false; return; }
      if (Math.abs(dx) > 12 && Math.abs(dx) > Math.abs(dy) * 1.4) horizontal = true;
      if (horizontal && !still) {
        const edge = (dx > 0 && !prev) || (dx < 0 && !next) ? 0.12 : 0.35; // resist where there's no page
        main.style.transform = `translateX(${dx * edge}px)`;
        main.style.opacity = String(1 - Math.min(0.35, Math.abs(dx) / 900));
      }
    }, { passive: true });
    const settle = () => {
      main.style.transition = "transform 0.3s cubic-bezier(.2,.7,.2,1), opacity 0.3s";
      main.style.transform = "";
      main.style.opacity = "";
    };
    main.addEventListener("touchend", () => {
      if (!tracking) return;
      tracking = false;
      const fast = performance.now() - t0 < 700;
      const far = Math.abs(dx) > Math.min(110, innerWidth * 0.22);
      if (horizontal && (far || (fast && Math.abs(dx) > 60))) {
        if (dx < 0 && next) return go(next, "forward");
        if (dx > 0 && prev) return go(prev, "back");
      }
      settle();
    });
    main.addEventListener("touchcancel", () => { tracking = false; settle(); });
  }

  // --- Copy buttons on code ---
  for (const btn of document.querySelectorAll(".code-block .copy")) {
    btn.addEventListener("click", async () => {
      const code = btn.parentElement.querySelector("code").innerText;
      try { await navigator.clipboard.writeText(code); btn.textContent = "Copied"; }
      catch (e) { btn.textContent = "Select and copy"; }
      setTimeout(() => { btn.textContent = "Copy"; }, 1600);
    });
  }

  // --- Screenshots at full size (click; Esc or click again closes) ---
  document.addEventListener("click", (e) => {
    const a = e.target.closest("a.zoom");
    if (!a || phone()) return; // phones: the image already fills the width; pinch to zoom
    e.preventDefault();
    const box = document.createElement("div");
    box.className = "zoom-view";
    box.innerHTML = `<img src="${a.getAttribute("href")}" alt="">`;
    box.querySelector("img").alt = a.querySelector("img").alt;
    const close = () => { box.classList.remove("open"); setTimeout(() => box.remove(), still ? 0 : 220); };
    box.addEventListener("click", close);
    document.addEventListener("keydown", function esc(ev) {
      if (ev.key === "Escape") { close(); document.removeEventListener("keydown", esc); }
    });
    document.body.appendChild(box);
    requestAnimationFrame(() => box.classList.add("open"));
  });

  // --- Search (search-index.js: one entry per section) ---
  const q = document.getElementById("guide-q"), results = document.getElementById("guide-results");
  if (q && results) {
    const esc = (s) => s.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
    const run = () => {
      const words = q.value.toLowerCase().split(/\s+/).filter(Boolean);
      const index = window.GUIDE_INDEX || [];
      if (!words.length) { results.hidden = true; results.innerHTML = ""; return; }
      const hits = [];
      for (const e of index) {
        const hay = (e.h + " " + e.t).toLowerCase();
        if (!words.every((w) => hay.includes(w))) continue;
        let score = 0;
        for (const w of words) {
          if (e.h.toLowerCase().includes(w)) score += 20;
          if (e.c.toLowerCase().includes(w)) score += 5;
          score += Math.min(10, hay.split(w).length - 1);
        }
        hits.push([score, e]);
      }
      hits.sort((a, b) => b[0] - a[0]);
      results.hidden = false;
      if (!hits.length) { results.innerHTML = `<p class="none">Nothing in the guide matches "${esc(q.value)}".</p>`; return; }
      results.innerHTML = hits.slice(0, 12).map(([, e]) => {
        const low = e.t.toLowerCase(), at = Math.max(0, low.indexOf(words[0]));
        let snip = e.t.slice(Math.max(0, at - 50), at + 90);
        snip = (at > 50 ? "..." : "") + esc(snip) + "...";
        for (const w of words) snip = snip.replace(new RegExp(w.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "gi"), (m) => `<mark>${m}</mark>`);
        const href = e.p + (e.a ? "#" + e.a : "");
        return `<a href="${href}"><span class="r-chapter">${esc(e.c)}</span><span class="r-section">${esc(e.h)}</span>` +
               `<span class="r-snip">${snip}</span></a>`;
      }).join("");
    };
    q.addEventListener("input", run);
    q.addEventListener("keydown", (e) => {
      if (e.key === "Escape") { q.value = ""; run(); }
      if (e.key === "Enter") { const first = results.querySelector("a"); if (first) location.href = first.href; }
    });
    document.addEventListener("keydown", (e) => {
      if (e.key === "/" && !e.target.closest("input, textarea")) {
        e.preventDefault();
        if (toggle && getComputedStyle(toggle).display !== "none" && toggle.getAttribute("aria-expanded") !== "true") toggle.click();
        q.focus();
      }
    });
  }

  // --- Highlight the section being read in "On this page" ---
  const links = [...document.querySelectorAll(".on-page a")];
  if (links.length && "IntersectionObserver" in window) {
    const byId = new Map(links.map((a) => [a.getAttribute("href").slice(1), a]));
    const spy = new IntersectionObserver((entries) => {
      for (const e of entries) {
        if (!e.isIntersecting) continue;
        for (const a of links) a.classList.remove("active");
        byId.get(e.target.id)?.classList.add("active");
      }
    }, { rootMargin: "-10% 0px -75% 0px" });
    for (const id of byId.keys()) { const h = document.getElementById(id); if (h) spy.observe(h); }
  }
})();
