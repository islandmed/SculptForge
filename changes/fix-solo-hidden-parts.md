---
whats_new: **Solo** (Tool > Parts) puts back exactly the Parts that were shown before it: a Part you had hidden stays hidden.
fixed: Solo in the Parts panel now remembers which Parts were shown when it began and restores exactly those on exit, so Parts hidden beforehand stay hidden (also across save and load while in Solo). Thanks @MasterFULL
testers:
  - Hide a Part, select another and press Solo, then Solo again: the first Part is still hidden, the others are back.
---
Root cause: part_solo kept no state, so leaving Solo showed every Part. Part::shownBeforeSolo holds the entry visibility (saved as shown_before_solo); Unhide all ends it.
