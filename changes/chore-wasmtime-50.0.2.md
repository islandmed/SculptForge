---
security: Plugin sandbox runtime updated (security fix): wasmtime 50.0.1 to 50.0.2 (#219), which fixes a guest-triggered host abort and unbounded host memory growth.
---
Fast lane: 50.0.2 (2026-11-16) fixes 8 advisories; two let a Wasm guest abort the host (GHSA-43j2-gcp4-m22g stale GC heap base, GHSA-wvgq-g5hf-6gm6 GC heap reservation).
