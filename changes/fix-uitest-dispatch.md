---
added: **Tooling**: the UI test runner's step dispatch is a verb table and a switch (a 121-link `else if` chain hit MSVC's block-nesting
  limit, C1061, on the Windows build), and `scripts/check_else_if_chains.py` fails any `else if` chain of 100 or more under src/.
---
Behaviour of every .sfui step is unchanged; the unreachable second `action` branch is gone.
