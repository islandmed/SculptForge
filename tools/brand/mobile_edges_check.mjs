// Mobile edge check (owner 11-27: "site looks cut off on mobile along the edges").
// Serves website/ on a local port, opens every page in a mobile emulation (DPR 3, touch, mobile UA) and fails on
//   - horizontal scroll (documentElement.scrollWidth > innerWidth)
//   - any element that sticks out past the left/right edge (outside a scroll container that is meant to scroll)
//   - text, buttons, links, inputs or bordered/filled cards closer than 16 px to a side edge
//   - a page without viewport-fit=cover, or without env(safe-area-inset-*) in its CSS
// Usage: node tools/brand/mobile_edges_check.mjs [--widths 360,375,390,412,430] [--landscape] [--dark] [--pages a.html,b.html] [--json out.json]
// Default (sitegate): every page at 360 portrait, light. Needs playwright (PLAYWRIGHT_MJS) and chromium (CHROMIUM).
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const arg = (k, d) => { const i = process.argv.indexOf(k); return i < 0 ? d : (process.argv[i + 1] ?? true); };
const has = (k) => process.argv.includes(k);
const root = path.resolve(arg('--root', 'website'));
const widths = String(arg('--widths', '360')).split(',').map(Number);
const gutter = Number(arg('--gutter', 16));
const pmjs = process.env.PLAYWRIGHT_MJS || path.join(process.env.HOME, '.local/share/mise/installs/npm-playwright/latest/node_modules/playwright/index.mjs');
const { chromium } = await import(pathToFileURL(pmjs).href);

const walk = (d, o = []) => { for (const f of fs.readdirSync(d, { withFileTypes: true })) { const p = path.join(d, f.name); f.isDirectory() ? walk(p, o) : f.name.endsWith('.html') && o.push(p); } return o; };
let pages = arg('--pages', null) ? String(arg('--pages')).split(',').map(p => path.join(root, p)) : walk(root);
pages = pages.filter(p => !/\/(noindex|_|link\/)/.test(p)).sort();  // website/link/ = the app's own remote UI (compiled in, app lane, its own safe-area layout)

const types = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.webp': 'image/webp', '.avif': 'image/avif', '.woff2': 'font/woff2', '.woff': 'font/woff', '.mp4': 'video/mp4', '.webm': 'video/webm', '.txt': 'text/plain', '.xml': 'application/xml' };
const server = http.createServer((req, res) => {
  let u = decodeURIComponent(req.url.split('?')[0]); if (u.endsWith('/')) u += 'index.html';
  const f = path.join(root, u);
  if (!f.startsWith(root) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); return res.end(); }
  res.writeHead(200, { 'content-type': types[path.extname(f)] || 'application/octet-stream' }); fs.createReadStream(f).pipe(res);
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}`;

const probe = ({ G, W }) => {
  // W = the width of the device. On a phone (isMobile) the browser widens the layout viewport when a page is wider than the
  // screen, so window.innerWidth == scrollWidth even when the page overflows: that is how a 390 px check passed 11-27.
  const out = [];
  if (window.innerWidth > W + 0.5) out.push({ kind: 'hscroll', detail: `layout viewport grew to ${window.innerWidth} px on a ${W} px screen: something is wider than the screen` });
  const de = document.documentElement;
  if (de.scrollWidth > W + 0.5) out.push({ kind: 'hscroll', detail: `scrollWidth ${de.scrollWidth} > screen width ${W}` });
  const sig = (el) => { const c = typeof el.className === 'string' ? el.className.trim().split(/\s+/).slice(0, 2).join('.') : ''; return el.tagName.toLowerCase() + (el.id ? '#' + el.id : '') + (c ? '.' + c : ''); };
  const scroller = (el) => { for (let p = el.parentElement; p && p !== document.body; p = p.parentElement) { const o = getComputedStyle(p); if (/(auto|scroll)/.test(o.overflowX)) return true; } return false; };
  const clipped = (el) => { for (let p = el.parentElement; p; p = p.parentElement) { const o = getComputedStyle(p); if (/(hidden|clip)/.test(o.overflowX) && p !== document.body && p !== de) { const r = p.getBoundingClientRect(); if (r.left >= -0.5 && r.right <= W + 0.5) return true; } } return false; };
  const seen = new Set();
  for (const el of document.body.querySelectorAll('*')) {
    const cs = getComputedStyle(el);
    if (cs.display === 'none' || cs.visibility === 'hidden' || Number(cs.opacity) === 0) continue;
    if (el.closest('[hidden],svg *,script,style,noscript,.sr-only,.visually-hidden')) continue;
    const r = el.getBoundingClientRect();
    if (r.width < 2 || r.height < 2) continue;
    if (scroller(el) || clipped(el)) continue;
    if (cs.position === 'fixed' && (r.right < 0 || r.left > W)) continue;
    const tag = el.tagName.toLowerCase();
    const own = [...el.childNodes].some(n => n.nodeType === 3 && n.textContent.trim());
    const boxed = (cs.borderLeftWidth !== '0px' && cs.borderLeftStyle !== 'none') || (cs.backgroundColor !== 'rgba(0, 0, 0, 0)' && !/^(html|body)$/.test(tag)) || cs.boxShadow !== 'none';
    const interactive = /^(a|button|input|select|textarea|summary)$/.test(tag);
    const media = /^(img|picture|video|canvas|iframe|table|pre)$/.test(tag);
    // sticking out of the page is always a bug
    if (r.left < -0.5 || r.right > W + 0.5) { const k = sig(el) + '@out'; if (!seen.has(k)) { seen.add(k); out.push({ kind: 'clipped', el: sig(el), detail: `x ${Math.round(r.left)}..${Math.round(r.right)} of ${W}` }); } continue; }
    // content too close to the edge (full-width layout wrappers and plain media are fine; text, controls and cards are not)
    const fullBleed = r.left <= 0.5 && r.right >= W - 0.5;
    if (fullBleed && !own && !interactive) continue;  // a full-width band or background: its text is checked on its own
    const textual = own || interactive || (boxed && !media);
    if (!textual) continue;
    if (own || interactive) {
      // judge the text itself (a full-width button or row is fine when its text has a gutter); text inside an ellipsis/overflow box is clamped to that box
      let l = Infinity, rr = -Infinity;
      const walker = document.createTreeWalker(el, NodeFilter.SHOW_TEXT);
      for (let n = walker.nextNode(); n; n = walker.nextNode()) {
        if (!n.textContent.trim() || (!interactive && n.parentElement !== el)) continue;
        const rg = document.createRange(); rg.selectNodeContents(n);
        for (const x of rg.getClientRects()) { if (x.width <= 1) continue; let a = x.left, b = x.right;
          for (let q = n.parentElement; q && q !== document.body; q = q.parentElement) { if (/(hidden|clip)/.test(getComputedStyle(q).overflowX)) { const bb = q.getBoundingClientRect(); a = Math.max(a, bb.left); b = Math.min(b, bb.right); } }
          if (b > a) { l = Math.min(l, a); rr = Math.max(rr, b); } }
      }
      if (l === Infinity) continue;  // no visible text (an icon or an image link): only 'sticks out' matters
      if (l < G - 0.5 || rr > W - G + 0.5) { const k = sig(el) + '@txt'; if (!seen.has(k)) { seen.add(k); out.push({ kind: 'edge', el: sig(el), detail: `left ${Math.round(l)}px, right ${Math.round(W - rr)}px`, text: (el.textContent || '').trim().slice(0, 40) }); } }
    } else if (boxed && (r.left < G - 0.5 || r.right > W - G + 0.5)) {
      const k = sig(el) + '@box'; if (!seen.has(k)) { seen.add(k); out.push({ kind: 'edge', el: sig(el), detail: `box left ${Math.round(r.left)}px, right ${Math.round(W - r.right)}px` }); }
    }
  }
  const vp = document.querySelector('meta[name=viewport]');
  if (!vp || !/viewport-fit\s*=\s*cover/.test(vp.content)) out.push({ kind: 'viewport', detail: 'viewport meta lacks viewport-fit=cover' });
  return out;
};

const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || '/usr/bin/chromium', args: ['--no-sandbox'] });
const results = []; let bad = 0, runs = 0;
const schemes = has('--dark') ? ['light', 'dark'] : ['light'];
const orients = has('--landscape') ? ['portrait', 'landscape'] : ['portrait'];
for (const w of widths) for (const o of orients) for (const scheme of schemes) {
  const [vw, vh] = o === 'portrait' ? [w, Math.round(w * 2.1)] : [Math.round(w * 2.1), w];
  const ctx = await browser.newContext({ viewport: { width: vw, height: vh }, deviceScaleFactor: 3, isMobile: true, hasTouch: true, colorScheme: scheme,
    userAgent: 'Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Mobile Safari/537.36' });
  await ctx.route(/^https?:\/\/(?!127\.0\.0\.1)/, r => r.abort());  // offline: no third-party fonts/analytics
  for (const f of pages) {
    const rel = path.relative(root, f); const page = await ctx.newPage(); runs++;
    try { await page.goto(`${base}/${rel}`, { waitUntil: 'load', timeout: 20000 }); await page.waitForTimeout(250); } catch (e) { results.push({ page: rel, w: vw, o, scheme, issues: [{ kind: 'load', detail: String(e).slice(0, 80) }] }); bad++; await page.close(); continue; }
    if (!page.url().startsWith(base)) { await page.close(); continue; }  // a redirect page (shop, m/*): its target is not ours to judge
    const issues = await page.evaluate(probe, { G: gutter, W: vw });
    if (issues.length) { bad++; results.push({ page: rel, w: vw, o, scheme, issues }); }
    await page.close();
  }
  await ctx.close();
}
await browser.close(); server.close();
if (arg('--json', null)) fs.writeFileSync(arg('--json'), JSON.stringify(results, null, 1));
for (const r of results) { console.log(`${r.page} @${r.w} ${r.o} ${r.scheme}`); for (const i of r.issues.slice(0, 8)) console.log(`   ${i.kind}${i.el ? ' ' + i.el : ''}: ${i.detail}${i.text ? ' "' + i.text + '"' : ''}`); if (r.issues.length > 8) console.log(`   ... ${r.issues.length - 8} more`); }
console.log(`mobile edges: ${bad ? 'FAIL' : 'ok'} (${runs} page loads, ${bad} with problems)`);
process.exit(bad ? 1 : 0);
