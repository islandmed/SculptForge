// SculptForge - the Studio launch offer, the one switch on the site (src/app/Support.h has the app's).
// Founding Studio: the first 10 studios lock in $150 / month for as long as they stay subscribed. The pages are written
// for the open offer, so they read right without scripts; when the 10 are taken, set FOUNDING_STUDIO_OPEN = false and
// every Studio button opens the $300 checkout, the price reads $300 and the founding badge and notes hide.
const FOUNDING_STUDIO_OPEN = true;
const STUDIO_URL = 'https://buy.stripe.com/9B66oH8uXd9w16j2254F205'; // $300 / month
if (!FOUNDING_STUDIO_OPEN) {
  document.querySelectorAll('[data-studio-link]').forEach((a) => { a.href = STUDIO_URL; });
  document.querySelectorAll('[data-studio-price]').forEach((e) => { e.textContent = '$300'; });
  document.querySelectorAll('[data-founding]').forEach((e) => { e.hidden = true; });
}
