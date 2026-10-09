# Docs style for this repo

Audience: me, re-reading my own lab notes. Concept-first, short.

- Each lab section: 1-line question (often "X vs Y") → how to run → 2–4 lines of what you see
  → small ASCII mental model → 1–2 "so what" bullets.
- Every lab must show how to run it.
  - Local macOS: `uv run torchrun --nproc-per-node=N --master_addr=127.0.0.1 --master_port=29500 -m <module>`
    (never `--standalone` locally; it is fine inside podman/Linux containers).
- No tables of raw measured numbers. State results as ratios/relations ("≈4× faster", "inflated N×", "no speedup").
- Long background goes in <details> or is cut if another section already covers it.
- Never describe my employer's systems in this repo.
- Docs-only changes must not modify code.
