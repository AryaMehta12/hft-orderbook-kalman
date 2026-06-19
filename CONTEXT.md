# CONTEXT.md — Project Working Document

> **For any Claude instance reading this:** This document is the complete context for an ongoing software project. Read it fully before responding. The user has been working with another Claude instance (Pro, on a different machine) for the design phase; you are now continuing the build phase.
>
> **Most of this document is immutable.** Only Sections 7, 12, and 13 are mutable. Section 4 is append-only. See Section 8 for full edit rules.
>
> **Do not propose redesigns.** Do not introduce features that have been explicitly rejected. The user leads architectural decisions; you implement and explain.
>
> **If anything in this document feels stale or contradicts a current user request, the locked decisions in Section 4 win.** Ask the user before deviating.

---

## 1. Who is the user

- 2nd year undergraduate at IIT Kanpur, Mathematics and Scientific Computing
- CPI 9.4, Department Rank 3
- Strong C++ (classes, STL, templates), medium Python
- Math background: linear algebra, probability, real analysis — done formally
- Codeforces Expert, 220+ LeetCode solved
- Currently interning at IISc on formal verification (IC3 / CEGAR / SAT solving)
- Target: foreign HFT internships (Optiver, Citadel, Jane Street, Jump Trading)
- Weak on: numerical methods, finance domain knowledge (learning by building)

**Calibration:** Explain concepts properly but don't over-simplify math. The user knows what a matrix inverse is, what a Bayesian posterior is, what a hash map is. The user does NOT know what an ITCH feed is, what bid-ask bounce is, or how matching engines are typically structured. Finance and microstructure need explanation; CS and math do not.

---

## 2. The project — one paragraph

A single-symbol limit order book matching engine in C++ paired with a 1D Kalman filter in Python. The C++ engine generates and processes a synthetic-but-microstructurally-realistic event stream (limit orders, cancels, market orders) and outputs a time series of noisy mid-prices contaminated by bid-ask bounce. The Python layer applies a Kalman filter to recover the latent "true" price from the noisy observations, validates against synthetic ground truth (possible because the true price is known to the experimenter), and compares to an exponential smoothing baseline. Deliverable is a polished GitHub repo with C++ source, a Jupyter notebook telling the analytical story, and a README. Two day core build, third day for polish and stretch goals.

---

## 3. Why this project

The user's friend is building a Monte Carlo option pricer in C++/Python (variance reduction, Greeks, Cholesky for correlated assets). This project is designed to be **orthogonal in skills demonstrated**: where the MC pricer is forward simulation + stochastic processes, this project is event-driven systems + state-space inference. Together they tell the story "I can build the infrastructure (order book), and I can extract signal from market data (Kalman)." Keep this orthogonality framing in mind when helping write the README or notebook — it's the project's narrative anchor.

The user explicitly evaluated and rejected:
- Pure order book without Kalman layer — too crowded on GitHub, lower intellectual ceiling
- Kalman / HMM / Markov chain projects without systems component — weaker resume bullet
- Avellaneda-Stoikov market making — too similar to friend's MC project (same skill set)
- Tabular Q-learning market maker — signals textbook RL not real quant work
- Particle filter / MCMC — too risky for 2-day timeline given no prior MCMC experience
- EM for Kalman parameter learning — pushed to STRETCH (build core first)

---

## 4. Locked design decisions — DO NOT REOPEN

### C++ engine
- **Single symbol** ("AAPL"-like, true price starts at $150). Multi-symbol explicitly out of scope.
- **Integer ticks**: prices stored as `int64_t`, tick size = 1 cent ($150.00 → 15000).
- **Engine assigns order IDs** internally via monotonic `uint64_t` counter.
- **Event types supported**: ADD_LIMIT, ADD_MARKET, CANCEL. No MODIFY, no REPLACE.
- **Market order overflow**: if a market order exceeds available liquidity, fill what's possible, discard the remainder (IOC semantics).
- **Output**: two CSVs — `midprices.csv` (timestamp, best_bid, best_ask, mid_price, true_price) and `trades.csv` (timestamp, aggressive_id, passive_id, price, quantity).
- **Verbose flag**: `--verbose` CLI flag prints per-event activity for debugging (event type, order ID, price, quantity, resulting book state). Default silent. Verbose mode is slow due to terminal I/O — never enable during benchmarks.
- **Benchmark binary**: separate from main, measures throughput and per-event latency (p50/p99/p99.9).
- **RNG**: seeded with fixed default (`42`) for reproducibility, overridable via `--seed <int>` CLI flag.
- **Code layout convention**: declarations in `.h` files, definitions in `.cpp` files. No inline implementations in headers except for templates and trivial getters.

### Synthetic event generator
- **True price**: slow Brownian motion (small step variance per event). Specific variance values deferred to Day 1 Block 4; tune so the price drifts visibly over 100K events but doesn't go negative.
- **Event mix**: 52% ADD_LIMIT, 40% CANCEL, 8% ADD_MARKET (calibrated to real microstructure).
- **Price clustering**: limit orders concentrated near touch, Gaussian-ish density falling off with distance from mid. Within ±10 ticks dense; sparse beyond.
- **Quantity distribution**: small integers (1–500 shares) with skew toward small sizes.
- **Default events**: 100K for analysis CSV (may bump to 500K-1M if statistical tests need more samples — see Section 12), 1M for benchmark.

### Python / Kalman side
- **State**: 1D (price only). 2D (price + drift) is future work.
- **Observation**: 1D (mid-price).
- **Resampling**: irregular event timestamps → uniform 1ms grid before filtering. Take last mid-price in each bin.
- **Q and R**: hand-tuned in Day 2; R estimated from short-horizon mid-price variance, Q chosen small. EM learning is STRETCH.
- **Baseline**: exponential smoother for comparison.
- **Ground truth**: since data is synthetic, compare both estimators against the *known* true price. This is a deliberate methodological choice and is a **strength** of the project, not a limitation. Frame it that way in the notebook and README.
- **Tests**: variance reduction, MSE vs ground truth, short-horizon predictive MSE, residual whiteness check.

### Data structures (C++)
- **Price levels**: `std::map<int64_t, PriceLevel, std::greater<>>` for bids (descending), `std::map<int64_t, PriceLevel, std::less<>>` for asks (ascending). Each `PriceLevel` holds a FIFO queue of orders at that price.
- **Order lookup**: `std::unordered_map<uint64_t, /* iterator into price-level deque */>` for O(1) cancels.
- **Matching**: price-time priority. Walk best price level, FIFO within level, walk deeper levels if liquidity insufficient.

### Testing approach
- Lightweight, no external test framework (no gtest / Catch2). Use plain `assert()` macros in `tests/test_orderbook.cpp`, compiled as a separate executable target in CMakeLists.
- Tests cover: add limit order to empty book, add crossing limit order (immediate trade), full match, partial fill, cancel, market order with insufficient liquidity, walking the book across price levels.
- Not exhaustive — just enough to trust each piece before composing them.

### Build and tooling
- **Build system**: CMake.
- **Compiler**: GCC via MinGW on Windows; user is on Windows native.
- **IDE**: VS Code with C/C++ extension, CMake Tools extension, CMake (syntax) extension.
- **Style**: camelCase functions, snake_case variables.
- **Git**: atomic commits at logical boundaries. **Push after every block.** Pull at start of every session.

### Presentation
- **Notebook style**: mix of research notebook (motivation, hypothesis, result) and engineering walkthrough.
- **LaTeX math** visible in the notebook for Kalman equations.
- **Repo visibility**: private during build, public on Day 3 when polished.
- **Final deliverable level**: Level 2 (notebook + README + plots). Level 3 (Streamlit) is STRETCH.

### Plots to produce
1. Raw mid-price vs Kalman-smoothed price (time series overlay)
2. Ground truth vs Kalman vs EMA (the killer plot — only possible because synthetic)
3. Variance reduction bar chart (raw vs EMA vs Kalman)
4. Residual histogram (raw - smoothed) with normality test overlay
5. Predictive MSE comparison (Kalman vs EMA at different horizons)
6. Latency histogram from C++ benchmark
7. Throughput chart

---

## 5. Repo layout

```
hft-orderbook-kalman/
├── README.md
├── CONTEXT.md                  ← this file
├── .gitignore
├── LICENSE
│
├── cpp/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── Order.h             ← Order, Trade, Event structs
│   │   ├── OrderBook.h
│   │   └── EventGenerator.h
│   ├── src/
│   │   ├── OrderBook.cpp
│   │   ├── EventGenerator.cpp
│   │   ├── main.cpp            ← runs simulation, writes CSVs
│   │   └── benchmark.cpp       ← throughput / latency
│   └── tests/
│       └── test_orderbook.cpp
│
├── data/                       ← generated CSVs (gitignored)
│
├── python/
│   ├── kalman.py
│   ├── analysis.ipynb
│   ├── requirements.txt
│   └── plots/
│
└── docs/
    └── architecture.png
```

---

## 6. The 3-day plan

### Day 1 (13 hours) — C++ Order Book
- **Block 1 (2h):** Context, design sketch, decide event/output formats.
- **Block 2 (3h):** Core structs (Order, PriceLevel, Trade), OrderBook class skeleton with add/cancel/query methods. Tiny correctness test.
- **Block 3 (3h):** Matching engine — partial fills, walking the book, edge cases. Tests.
- **Block 4 (2h):** Synthetic event generator + main loop wiring. CSV output.
- **Block 5 (2h):** Benchmark binary, throughput and latency measurement.
- **Block 6 (1h):** Buffer / cleanup / sanity check the CSVs in Python briefly.

**End-of-Day-1 deliverable:** C++ engine compiles, runs, produces sane `midprices.csv` and `trades.csv`, benchmark prints throughput and latency.

### Day 2 (13 hours) — Kalman Filter + Analysis
- **Block 1 (2h):** Kalman math on paper. Predict, update, gain derivation. Hand-work a tiny example.
- **Block 2 (2h):** Implement filter in NumPy. Unit test with known input/output.
- **Block 3 (1.5h):** Apply to order book output. First overlay plot.
- **Block 4 (1.5h):** Parameter selection. Estimate R from data. Try a few Q values.
- **Block 5 (3h):** Quantitative evaluation — variance reduction, predictive MSE, ground-truth comparison, residual whiteness.
- **Block 6 (1h):** RTS smoother (optional within Day 2).
- **Block 7 (2h):** Plot polish, save all PNGs.

**End-of-Day-2 deliverable:** All technical work done. Results computed. Plots saved.

### Day 3 (6 hours) — Polish
- **Block 1 (3h):** Build the Jupyter notebook narrative.
- **Block 2 (2h):** Finalize README with real numbers.
- **Block 3 (1h):** GitHub polish — directory structure, .gitignore, clean clone test, make repo public.

### Stretch goals (in priority order)
1. EM algorithm for Q, R learning (~4–6h)
2. RTS smoother (~1h if not done Day 2)
3. Streamlit dashboard (~4–6h)
4. ITCH parser for real data (~3–4h)

Pick at most one.

---

## 7. STATUS — UPDATE THIS AS YOU PROGRESS

```
DAY:                  [1 / 2 / 3]
BLOCK:                [block number and name, e.g., "Block 3 — matching engine"]
CURRENT STATUS:       [one sentence — what's happening right now]
LAST COMPLETED:       [one sentence — what just got finished]
NEXT IMMEDIATE STEP:  [one sentence — exact next action]
KNOWN ISSUES:         [bugs / blockers, or "None"]
DEFERRED:             [things pushed to later, or "None"]
LAST PUSH:            [timestamp or "not yet pushed"]
```

**Update format:** Overwrite the values in place. Don't append to the file. Don't add commentary outside the block. Just keep this snapshot current.

**At end of every block:** update this section, then commit and push CONTEXT.md to GitHub along with any code changes. The `LAST PUSH` field tracks this.

---

## 8. What you may and may not modify in this document

This document is a working artifact. Some sections are mutable; most are not. Follow these rules strictly.

### MUTABLE — update freely when relevant

- **Section 7 (STATUS)** — update at end of every work block. This is the primary mutable section.
- **Section 12 (Open questions / TODOs)** — add items as they arise, remove when resolved.
- **Section 13 (Resume bullet)** — fill in real numbers once measured on Day 2.

### APPEND-ONLY — never rewrite, only add

- **Section 4 (Locked design decisions)** — if the user explicitly confirms a new decision (after you've pushed back per Section 9 rule 7), append it to the relevant subsection with a note `[Added Day X: <reason>]`. Never delete or rewrite existing entries.
- **Section 6 (3-day plan)** — if the plan shifts (e.g., a block runs over), note the shift inline as `[Day X note: actual time was Yh due to Z]` rather than rewriting the block.

### IMMUTABLE — never modify under any circumstances

- The front-matter quote block at the top
- **Section 1 (Who is the user)**
- **Section 2 (The project — one paragraph)**
- **Section 3 (Why this project)**
- **Section 5 (Repo layout)** — if structure changes, the user will edit this themselves
- **Section 9 (Rules of engagement)**
- **Section 10 (When to escalate)**
- **Section 11 (Git cheatsheet)**
- **This Section 8 itself**

### If the user asks you to edit something marked IMMUTABLE

Push back: *"That section is marked immutable in CONTEXT.md. If you want to change it, edit the file yourself directly rather than asking me to. That's intentional — it prevents accidental drift."* If they confirm they really want it changed, they should do it themselves outside the conversation.

### If you're not sure whether to edit something

Default to **not editing**. Ask the user first. Drift in this document compounds badly across sessions.

### Edit hygiene

- When you do edit, edit only the section asked for. Do not "improve" adjacent sections.
- Preserve the exact heading structure (`##`, `###`) and section numbering.
- After any edit, show the user the diff (just the changed lines, not the whole file) and ask them to confirm before they save it.

---

## 9. Rules of engagement

The user is leading this build. Your role is:

1. **Implement what's been decided. Don't propose redesigns.** If the user asks about a design choice already locked in Section 4, explain the reasoning that's already in this doc rather than suggesting alternatives.

2. **Don't write large code blocks without explaining them first.** The user must be able to defend every line. For each function: explain the intent, the algorithm, then write the code, then walk through any non-obvious lines.

3. **THE USER WRITES THE ALGORITHMIC CORE THEMSELVES.** This is the most important rule. The following functions must be written *by the user*, not by you:
   - `OrderBook::matchOrder` (the matching loop)
   - The Kalman `predict` step
   - The Kalman `update` step
   - The EM updates (if the stretch goal is attempted)

   For these, you provide: pseudocode, edge-case lists, math derivations, code review after the user writes. You do NOT produce these as finished code blocks for the user to paste. This is defensibility insurance — the user must own these specific pieces.

4. **You can write boilerplate freely.** CMakeLists, .gitignore, CSV parsing, plotting code, struct definitions, test scaffolding, data loading, argument parsing — produce these directly.

5. **Surface tradeoffs explicitly.** When two approaches are reasonable, name both before picking. Don't silently choose.

6. **If unsure, ask.** Don't make architectural decisions on the user's behalf. Don't invent features. If the user says "add timestamp to events" and there are three ways to do it, ask.

7. **If the user asks for something that contradicts a locked decision in Section 4, push back once.** "We previously locked X for reason Y — are you sure you want to change this?" If user confirms, proceed and append the change to Section 4 per the append-only rule.

8. **At the end of every block, do all of the following:**
   - Prompt the user to update Section 7 (STATUS)
   - **Remind the user to `git add`, `git commit`, and `git push`** — this is non-negotiable, the user explicitly requested constant reminders. Phrase like: *"End of block. Update STATUS section in CONTEXT.md, then commit and push everything (code + CONTEXT.md) to GitHub before continuing."*
   - If the user hasn't pushed in the last 90 minutes, remind them mid-block too

9. **Stay calibrated to the user's level.** This is a 2nd year strong undergrad — don't condescend, don't over-explain CS fundamentals, but do explain microstructure and finance concepts that are new.

10. **Don't pad responses.** No "Great question!" preambles, no recap of what was just discussed, no bullet lists when prose works. The user values directness.

---

## 10. When to escalate back to System A (Pro Claude)

You (System B Claude) are good for code generation and known-pattern implementation. The user should return to System A for:

- **Genuinely tricky design questions** that weren't covered in this doc
- **Math derivations** beyond Kalman update (e.g., RTS smoother, EM derivation)
- **Strategic decisions** (e.g., "should I attempt the EM stretch goal or polish further")
- **Anything that feels like it might contradict the locked design**

When the user mentions escalating, just acknowledge and let them switch. Do not try to handle questions you're unsure about.

---

## 11. Git cheatsheet

**Daily flow (95% case):**

```
# Start of session
git pull

# As you work, commit at logical boundaries
git add <files>
git commit -m "short description"

# End of block — always push
git push
```

**"git status" is the universal first move when confused.** Shows what's staged, modified, untracked, and which branch you're on. Run it before anything else if uncertain.

**"I forgot to pull and now push is rejected":**

```
git pull --rebase    # replays your local commits on top of remote
git push
```

If this produces a merge conflict in a file, open the file, find the `<<<<<<<` markers, edit to the version you want, save, then:

```
git add <file>
git rebase --continue
git push
```

**"I committed something I shouldn't have, but didn't push yet":**

```
# Undo last commit, keep the file changes staged
git reset --soft HEAD~1

# Undo last commit and unstage the changes too (changes still in files)
git reset HEAD~1

# Nuke last commit and discard all changes (DANGEROUS)
git reset --hard HEAD~1
```

**"I have uncommitted changes but need to switch machines RIGHT NOW":**

```
git stash                  # saves changes aside
git push                   # sync any committed work
# ...switch machines, pull...
git stash pop              # restore the in-progress changes
```

(But `stash` is local to one machine. To sync in-progress work across machines, just commit it as `WIP: <description>` and clean up later.)

**"VS Code Source Control panel markers":**
- `U` = untracked (new file, not in Git yet)
- `M` = modified (Git knows it, you changed it)
- `A` = added (staged for commit)
- `D` = deleted
- Click the `+` next to a file to stage it. Click the checkmark at top to commit.

**Cardinal rules:**
- Run `git status` if you're confused.
- Push before switching machines. Always.
- Push at the end of every block. Always.
- Don't `git push --force` unless you fully understand it. (You won't need it on this project.)

---

## 12. Open questions / TODOs

- **Sample size for analysis CSV:** Default is 100K events. May need to bump to 500K-1M if statistical tests (residual whiteness, predictive MSE confidence intervals) need more samples. Decide on Day 2 Block 5.
- **Synthetic generator parameter values:** True-price step variance, exact quantity distribution shape, exact price-clustering bandwidth — tune empirically on Day 1 Block 4 so the resulting CSV looks reasonable. Document final values in code comments.

Add new items here as they arise. Remove items when resolved.

---

## 13. Resume bullet (target)

To be finalized after Day 2 with real numbers. Working draft:

> Built a C++ limit order book matching engine ([X]M events/sec, p99 [Y]ns latency) with synthetic microstructure-realistic event generation; applied Kalman filtering to extract latent mid-prices from noisy observed quotes, achieving [Z]% reduction in tick-to-tick variance and [W]× lower MSE vs ground truth compared to baseline exponential smoothing.

---

END OF CONTEXT.md
