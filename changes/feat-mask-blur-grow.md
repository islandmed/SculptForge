---
whats_new: **Each blur widens**: **Tool > Masking** has a new slider (1x to 3x, default 1x = as before). At 2x each Blur click in a row reaches twice as far as the one before, so a few clicks take a mask from a slightly soft edge to a very soft one.
member_request: true
added: **Each blur widens** (Tool > Masking, next to Passes): consecutive Blur clicks (the button or Ctrl+click on the model) on the same mask reach this many times further each click (1x to 3x; default 1x, so every click blurs the same as before). The chain ends when the mask changes by anything but a blur, after an undo, or after about 2 seconds without a click. Dense Parts cap the reach so each click stays quick. Every click is still one undo step.
testers:
  - Any OS: Tool > Masking, mask part of a model, set Each blur widens to 2x, then Ctrl+click the model three times quickly; each click softens the edge over about twice the width of the one before. At 1x every click is the same small amount.
---
Reviewers: the reach of a blur grows with the square root of its passes, so "2x" is 4x the passes per click (1, 4, 16, 64);
the pass count is capped at 100 and at 40 million vertex-passes per click (maskBlurClickPasses).
