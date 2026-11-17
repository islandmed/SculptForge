---
fixed: First start: the "Tuned for your machine" box answers its first tap or click. The membership card no longer opens on
  top of it (it waits, one box at a time: welcome, tier box, then the membership card), and no membership card, appeal or
  small card shows to a member or in the members-only Android build (for now). Reported by ebarranco.
testers:
  - Android tablet, first start: the "Tuned for your machine" box closes on the first tap; no membership window under it.
  - Desktop, new install: welcome, the tier box, then the membership card opens after Got it.
---
Queue: App::tickSupportCardQueue; tier box hides under a modal (App::tierCardCovered); App::pleaBlocked.
