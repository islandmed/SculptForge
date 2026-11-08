---
whats_new: **The tier card puts itself away**: **Got it**, its new close button, Esc or Enter dismiss the "Tuned for
  your machine" card on the first press, and it stays away, also on laptops with two graphics cards.
fixed: The first-run tier card ("Tuned for your machine"): **Got it** could do nothing on Linux, and the card came
  back on every start (#161, thanks @chipset-tech). A press that lands on the card the same frame the pointer gets
  there (a trackpad tap, a pen touching down, a click in one batch of events with the move) now always reaches its
  buttons, and never the model underneath; the card also has a close button and goes away with Esc or Enter. It is
  remembered for each graphics card a laptop starts on, and for memory sizes the driver reports a little differently
  between starts.
changed: No press on a floating card or panel over the 3D view starts a stroke underneath, even before the UI has
  seen the pointer arrive (the first-run cards, the Quick Tour, banners).
testers:
  - Linux, Wayland session (GNOME or KDE), ideally a laptop with Intel + NVIDIA: delete hardware_profile.json in
    the SculptForge settings folder and start; the "Tuned for your machine" card shows. Tap Got it once with the trackpad: it goes away at once, nothing is sculpted under it.
    Restart: it doesn't come back.
  - Linux, X11 session (or SDL_VIDEO_DRIVER=x11 under Wayland): the same, with a mouse click; then with the close (x)
    and with Esc, each after deleting hardware_profile.json again. UI scale 150 % and 200 % too.
  - Two-GPU laptop: set Preferences > Performance > Graphics card to the other card, restart, put the card away;
    switch back and restart: no card either time after the first on each card.
  - Windows with a pen: one tap of the pen on Got it works.
---
Reviewers: tests/ui/tier_card_click.sfui (every way, 100/150/200 %, model under the card, restart);
tests/test_hardware_tier.cpp hw_tier_card_seen.
