// SculptForge website: creator referral codes (docs/affiliates.md). A page opened with ?ref=<code> remembers the
// code for 30 days in this browser (localStorage; no cookies, nothing sent anywhere) and adds
// client_reference_id=<code> to the yearly Stripe checkout links (referrals count on yearly plans only), so the
// creator who sent you gets credit. A code is 1-32 of a-z 0-9 _ -; anything else is ignored. Monthly links, and
// every link without a code or without JavaScript, stay as they are.
(() => {
  // The yearly checkouts (Payment Link ids): Member $48 / year. Add a Studio yearly link here when it exists.
  const YEARLY = new Set(["aFa5kD4eHfhE2an6il4F206"]);
  const KEY = "sf-ref", DAYS = 30, valid = (c) => typeof c === "string" && /^[a-z0-9_-]{1,32}$/.test(c);
  let code = null;
  try {
    const given = new URLSearchParams(location.search).get("ref");
    if (given !== null && valid(given.toLowerCase())) {
      code = given.toLowerCase();
      localStorage.setItem(KEY, JSON.stringify({ code, t: Date.now() }));
    } else {
      const s = JSON.parse(localStorage.getItem(KEY) || "null");
      if (s && valid(s.code) && Number.isFinite(s.t) && Date.now() - s.t < DAYS * 864e5) code = s.code;
      else if (s) localStorage.removeItem(KEY);
    }
  } catch (e) { /* storage blocked or bad data: no code */ }
  if (!code) return;
  // Tag a yearly Stripe checkout link; also a link's yearly variant (support.html swaps href to data-yearly).
  const tag = (url) => {
    try {
      const u = new URL(url, location.href);
      if (u.protocol !== "https:" || u.hostname !== "buy.stripe.com" || !YEARLY.has(u.pathname.slice(1))) return url;
      u.searchParams.set("client_reference_id", code);
      return u.href;
    } catch (e) { return url; }
  };
  const tagLink = (a) => {
    const href = a.getAttribute("href");
    if (href) { const t = tag(href); if (t !== href) a.setAttribute("href", t); }
    if (a.dataset.yearly) a.dataset.yearly = tag(a.dataset.yearly);
  };
  const tagAll = () => document.querySelectorAll('a[href^="https://buy.stripe.com/"]').forEach(tagLink);
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", tagAll); else tagAll();
  // At click time too, for links whose href changed after load (the Monthly / Yearly switch).
  for (const ev of ["click", "auxclick", "contextmenu"]) {
    document.addEventListener(ev, (e) => {
      const a = e.target instanceof Element && e.target.closest('a[href^="https://buy.stripe.com/"]');
      if (a) tagLink(a);
    }, true);
  }
})();
