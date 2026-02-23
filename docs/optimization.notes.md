# Optimization Notes

Date: 2026-02-23

Current baseline from local benchmark work:
- `sml_pool_batch_random`: `0.472 ns/event`
- `direct_random`: `0.425 ns/event`
- Remaining gap: about `11%` on this workload

Prioritized opportunities:
1. Add a true internal batch fast path in core `sm` so event-sequence dispatch can amortize per-event plumbing.
2. Add a specialized `sm_pool` fast path for trivial indexed events to reduce wrapper overhead in hot loops.
3. Add a packed homogeneous-state pool policy (SoA-style) for large machine pools to improve cache locality.
4. Reduce compile-time cost with transition-trait/result caching and lower template-instantiation churn.
5. Add PGO/LTO benchmark profiles for release to improve inlining and branch prediction in dispatch hot paths.

Notes:
- Keep changes generic to SML internals (not tied to any single application event model).
- Evaluate with both local and random access patterns before/after each change.
