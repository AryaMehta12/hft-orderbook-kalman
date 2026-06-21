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
- **PriceLevel container**: `std::list<Order>` for the FIFO queue at each price level (not `std::queue`, not `std::deque`) — required so the order-lookup `unordered_map` can store stable iterators for O(1) cancel-by-ID anywhere in the queue, not just at the ends. [Added Session 1.]
- **C++ standard**: C++17, set via `CMAKE_CXX_STANDARD 17`. [Added Session 1.]
- **Order.timestamp semantics**: simulated elapsed nanoseconds since simulation start (generator accumulates random inter-arrival gaps), NOT wall-clock and NOT a plain counter. Needed for Day 2's "resample to uniform 1ms grid" step. [Added Session 1.]
- **Order struct stores price and side redundantly** alongside the map key/bucket. Safe because no MODIFY event type — price and side immutable after construction. If MODIFY ever added later, must be cancel-then-reinsert at the new price, never in-place mutation. [Added Session 1.]
- **PriceLevel lives in its own header** (`cpp/include/PriceLevel.h`), not nested inside `OrderBook.h`. [Added Session 2.]
- **No OrderType field on Order**; instead, OrderBook exposes two public methods `submitLimitOrder(Order)` and `submitMarketOrder(Order)`. Internally both delegate to private `matchOrder(Order&, bool stop_on_price)`. [Added Session 2.]
- **PriceLevel caches `total_quantity`**, maintained by OrderBook on every add/cancel/fill. Avoids O(n_at_level) sum walks. [Added Session 2.]
- **`OrderBook::isResting(uint64_t)` public inline accessor** — checks if an order id is currently in `order_index_`. Added to fix a partial-fill bug in main.cpp's resting-id tracking (trades for partially-consumed orders would otherwise incorrectly desync the tracker). [Added Session 3.]
- **Benchmark uses live event generation, not pre-generation**. Pre-generation caused the generator's cancel-target tracker to drift from book state, producing near-zero effective cancel rate and book bloating to 33K resting orders. Live generation in the timed loop with selective bracketing (only the book op is bracketed for per-event latency, gen.next() runs in the loop but outside the brackets) restores correctness. [Added Session 5.]
- **Benchmark reports both book-only and wall throughput**. Book-only = events / sum(per-event latencies) (engine's intrinsic speed); wall = events / total loop time (full pipeline including generator). [Added Session 5.]

### Synthetic event generator
- **True price**: slow Brownian motion (Gaussian step σ ≈ 1 tick per event). Updated every event. Clamped ≥ 1 tick.
- **Event mix**: 52% ADD_LIMIT, 40% CANCEL, 8% ADD_MARKET. CANCEL→ADD_LIMIT fallback if `resting_ids_` is empty.
- **Limit price clustering**: Gaussian offset σ ≈ 10 ticks around true price, biased to passive side (BUY below, SELL above) via `std::abs(offset)` signed by side. Most limits rest. Bid-ask bounce comes primarily from MARKET events.
- **Quantity distribution**: exponential with mean ≈ 50, clipped to [1, 500].
- **Inter-arrival times**: exponential with mean 1 µs per event (Poisson arrival process). 100K events → ~100 ms of simulated time.
- **Resting-id tracking**: generator owns a `std::vector<uint64_t>` of currently-resting ids. main.cpp keeps it synced via `registerResting` / `unregisterResting` after each book operation. Swap-and-pop for O(1) removal.
- **Default events**: 100K for analysis CSV (bumpable to 500K-1M if statistical tests need more samples — see Section 12), 1M for benchmark.

### Python / Kalman side
- **State**: 1D (price only). 2D (price + drift) is future work.
- **Observation**: 1D (mid-price).
- **Resampling**: irregular event timestamps → uniform 1ms grid before filtering. Take last mid-price in each bin.
- **Q and R**: hand-tuned in Day 2; R estimated from short-horizon mid-price variance, Q chosen small. EM learning is STRETCH.
- **Baseline**: exponential smoother for comparison.
- **Ground truth**: synthetic data has known true price; compare both estimators against it. Strength of the project, not a limitation. Frame as a deliberate methodological advantage.
- **Tests**: variance reduction, MSE vs ground truth, short-horizon predictive MSE, residual whiteness check (eyeball-level on the residual plot, not a formal statistical test — cut for time).
- **Filter implementation lives in `python/kalman.py`** (pure functions: `predict`, `update`, `run_filter`). Notebook imports from it; does not redefine the algorithm. This keeps the algorithmic core in its own defensible file per Rule 3.
- **Analysis is built incrementally in `python/analysis.ipynb` alongside the Python code** — no separate scratch-then-translate phase. Each notebook section is written as the corresponding step is done. By end of Day 2 the notebook is near-deliverable. [Added Session 6.]

### Data structures (C++)
- **Price levels**: `std::map<int64_t, PriceLevel, std::greater<int64_t>>` for bids (descending), `std::map<int64_t, PriceLevel, std::less<int64_t>>` for asks (ascending). Each `PriceLevel` holds a FIFO `std::list<Order>` queue plus cached `total_quantity`.
- **Order lookup**: `std::unordered_map<uint64_t, OrderLocator>` for O(1) cancels. `OrderLocator = {Side, int64_t price, list iterator}`.
- **Matching**: price-time priority. Walk best level, FIFO within level, walk deeper levels if liquidity insufficient.

### Testing approach
- Lightweight, no external test framework (no gtest / Catch2). Plain `assert()` macros in `tests/test_orderbook.cpp`, separate executable target.
- 15 tests total: 6 structural (add, cancel, query, FIFO, empty handling) + 9 matching (cross/full/partial fill, multi-level walk, FIFO priority during match, market orders, SELL-side mirror).
- Status: 15/15 passing as of end of Day 1.

### Build and tooling
- **Build system**: CMake.
- **Compiler**: GCC 16.1.0 via MinGW-w64 (UCRT64) on Windows native.
- **IDE**: VS Code with C/C++, CMake Tools, CMake (syntax highlighting) extensions.
- **Python**: 3.13 with JupyterLab, NumPy, pandas, matplotlib, scipy. Deps pinned in `python/requirements.txt`.
- **Style**: camelCase functions, snake_case local variables, camelCase struct field names (`Trade::aggressiveId`, `Trade::passiveId`), trailing-underscore for private class members.
- **Git**: atomic commits at logical boundaries. **Push after every block.** Pull at start of every session.

### Presentation
- **Notebook style**: research narrative + engineering walkthrough, built incrementally during Day 2.
- **LaTeX math** visible in the notebook for Kalman equations.
- **Repo visibility**: private during build, public on Day 3 when polished.
- **Final deliverable level**: Level 2 (notebook + README + plots). Level 3 (Streamlit) is STRETCH.
- **Resumepoints.md** at repo root is private scratch (gitignored). Appended to as each block completes; final CV bullets picked from it on Day 3.

### Plots to produce
1. Raw mid-price vs Kalman-smoothed price (time-series overlay)
2. Ground truth vs Kalman vs EMA (the killer plot — only possible because synthetic)
3. Variance reduction bar chart (raw vs EMA vs Kalman)
4. Residual histogram (raw - smoothed) with eyeballed normality
5. Predictive MSE comparison (Kalman vs EMA at different horizons)
6. Latency histogram from C++ benchmark
7. Throughput chart (or summary table — TBD which is cleaner)

---

## 5. Repo layout

```
hft-orderbook-kalman/
├── README.md
├── CONTEXT.md                  ← this file
├── Resumepoints.md             ← private scratch (gitignored)
├── .gitignore
├── LICENSE
│
├── cpp/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── Order.h             ← Order, Trade, Side
│   │   ├── PriceLevel.h        ← PriceLevel struct (FIFO queue per price)
│   │   ├── OrderBook.h         ← OrderBook class (matching engine)
│   │   └── EventGenerator.h    ← Event struct + EventGenerator class
│   ├── src/
│   │   ├── OrderBook.cpp
│   │   ├── EventGenerator.cpp
│   │   ├── main.cpp            ← runs simulation, writes CSVs
│   │   └── benchmark.cpp       ← throughput / latency measurement
│   └── tests/
│       └── test_orderbook.cpp
│
├── data/                       ← generated CSVs (gitignored)
│
├── python/
│   ├── kalman.py               ← filter implementation (predict/update/run)
│   ├── analysis.ipynb          ← incremental analysis notebook
│   ├── requirements.txt
│   └── plots/
│
└── docs/
    └── architecture.png
```

---

## 6. The 3-day plan

### Day 1 (13 hours) — C++ Order Book — COMPLETE
- **Block 1 (2h):** Context, design sketch, decide event/output formats.
- **Block 2 (3h):** Core structs (Order, PriceLevel, Trade), OrderBook class skeleton with add/cancel/query methods. Tiny correctness test.
- **Block 3 (3h):** Matching engine — partial fills, walking the book, edge cases. Tests.
- **Block 4 (2h):** Synthetic event generator + main loop wiring. CSV output.
- **Block 5 (2h):** Benchmark binary, throughput and latency measurement.
- **Block 6 (1h):** Buffer / cleanup / sanity check the CSVs in Python briefly.

**End-of-Day-1 deliverable:** C++ engine compiles, runs, produces sane CSVs, benchmark prints throughput and latency. **DELIVERED.**

[Day 1 note: Block 1 ran over due to toolchain setup — MinGW.org GCC 6.3.0 unusable, replaced with MinGW-w64 16.1.0 via MSYS2, CMake installed from scratch.]

[Day 1 note: Block 2 spread across sessions 2-3. Three headers + OrderBook.cpp + CMakeLists + 6 structural tests. Two design clarifications surfaced: PriceLevel split to own header, two-method submit API.]

[Day 1 note: Block 3 — user wrote matchOrder. 9 matching tests added. 15/15 passing.]

[Day 1 note: Block 4 done across sessions 3-5. EventGenerator + main wired up. User initially copy-pasted under fatigue; full intuitive walkthrough completed in session 6 covering Brownian price, Gaussian passive-biased limits, 52/40/8 event mix, CANCEL→LIMIT fallback, resting-id tracker, partial-fill bug fixed by isResting. User now defends the design in own words.]

[Day 1 note: Block 5 — benchmark.cpp. First attempt had a real bug (pre-generation desyncing the cancel tracker, book bloated to 33K resting, throughput 0.55M). Fixed via live generation in the timed loop with selective bracketing. Final numbers: 3.46M evt/s book-only, 1.82M wall, p50 200ns, p90 500ns, p99 1µs, p99.9 2.2µs, p99.99 23µs, max 450µs, 13 resting orders steady-state.]

### Day 2 (13 hours) — Kalman Filter + Analysis
- **Block 1 (1h):** Kalman math on paper. Predict, update, Kalman gain derivation from Bayes + Gaussian conjugacy. **USER DERIVES** per Rule 3.
- **Block 2 (1.5h):** Implement `kalman.py` — `predict`, `update`, `run_filter`. **USER WRITES** predict/update.
- **Block 3 (0.75h):** Notebook section: motivation + load midprices.csv + raw data viz.
- **Block 4 (0.75h):** Notebook section: apply Kalman, first overlay plot (raw vs smoothed vs ground truth).
- **Block 5 (0.75h):** Notebook section: parameter selection narrative (Q from prior belief, R estimated from data).
- **Block 6 (0.75h):** Notebook section: EMA baseline + comparison.
- **Block 7 (0.75h):** Notebook section: quantitative metrics — variance reduction, MSE vs ground truth, MSE comparison.
- **Block 8 (1h):** Notebook section: latency histogram + benchmark numbers, conclusions.
- **Block 9 (1h):** Plot polish, save all PNGs to `python/plots/`.

**End-of-Day-2 deliverable:** Notebook is near-deliverable with all sections, all plots, all numbers. README drafted with placeholders.

### Day 3 (3-4 hours) — Polish
- **Block 1 (1.5h):** Final notebook prose pass.
- **Block 2 (1h):** Final README with all numbers slotted in, design-decisions section, references.
- **Block 3 (0.5h):** Final Resumepoints.md sweep; pick 3-5 CV bullets.
- **Block 4 (0.5h):** GitHub polish — clean clone test, .gitignore review, make repo public.

### Stretch goals (in priority order)
1. EM algorithm for Q, R learning (~4–6h)
2. RTS smoother (~1h if not done Day 2)
3. Streamlit dashboard (~4–6h)
4. ITCH parser for real data (~3–4h)

Pick at most one. User's stated deadline is "90% finish today" with cleanup tomorrow.

---

## 7. STATUS — UPDATE THIS AS YOU PROGRESS

```
DAY:                  Day 1 COMPLETE / Day 2 starting
BLOCK:                Day 2 Block 1 — Kalman math on paper (about to start)
CURRENT STATUS:       C++ side fully done with real benchmark numbers. Python env set up (3.13, JupyterLab, NumPy/pandas/matplotlib/scipy installed via requirements.txt). Resumepoints.md created and gitignored.
LAST COMPLETED:       Day 1 final commit (benchmark.cpp + CMakeLists + CONTEXT.md). Python dependencies installed. Resumepoints.md drafted with Day 1 content. Decision locked: notebook built incrementally alongside code, not separately.
NEXT IMMEDIATE STEP:  Kalman math derivation on paper — user derives predict step, update step, and Kalman gain from Bayes + Gaussian conjugacy. Then implement in kalman.py.
KNOWN ISSUES:         None.
DEFERRED:             Notebook prose polish, README final, GitHub-public — Day 3.
LAST PUSH:            done — Day 1 final commit pushed.
```

**Update format:** Overwrite the values in place. Don't append. Don't add commentary outside the block.

**At end of every block:** update this section, then commit and push CONTEXT.md along with any code changes.

---

## 8. What you may and may not modify in this document

### MUTABLE — update freely
- **Section 7 (STATUS)** — update at end of every work block.
- **Section 12 (Open questions / TODOs)** — add as they arise, remove when resolved.
- **Section 13 (Resume bullet)** — fill in real numbers once measured.

### APPEND-ONLY — never rewrite, only add
- **Section 4 (Locked design decisions)** — append confirmed new decisions with `[Added Session N: <reason>]` notes.
- **Section 6 (3-day plan)** — note plan shifts inline as `[Day X note: ...]`, never rewrite the block.

### IMMUTABLE — never modify
- The front-matter quote block at the top
- Section 1 (Who is the user)
- Section 2 (The project — one paragraph)
- Section 3 (Why this project)
- Section 5 (Repo layout) — user edits this themselves if needed
- Section 9 (Rules of engagement)
- Section 10 (When to escalate)
- Section 11 (Git cheatsheet)
- This Section 8 itself

### If the user asks you to edit something marked IMMUTABLE
Push back: *"That section is marked immutable in CONTEXT.md. Edit the file yourself directly rather than asking me to."* If they confirm, they do it themselves.

### If you're not sure whether to edit something
Default to **not editing**. Ask first.

### Edit hygiene
- Edit only the section asked for. Don't "improve" adjacent sections.
- Preserve heading structure and section numbering.
- After any edit, show only the diff and ask confirmation before save.

---

## 9. Rules of engagement

1. **Implement what's been decided. Don't propose redesigns.** Explain reasoning already in this doc rather than suggesting alternatives.

2. **Don't write large code blocks without explaining them first.** For each function: intent, algorithm, code, then walk through non-obvious lines.

3. **THE USER WRITES THE ALGORITHMIC CORE THEMSELVES.** Most important rule.
   - `OrderBook::matchOrder` — DONE in Block 3.
   - The Kalman `predict` step
   - The Kalman `update` step
   - The EM updates (if stretch attempted)
   For these: pseudocode, edge cases, math, review. NEVER produce finished code blocks for the user to paste.

4. **You can write boilerplate freely.** CMakeLists, .gitignore, CSV parsing, plotting code, struct definitions, test scaffolding, data loading, argument parsing.

5. **Surface tradeoffs explicitly** when two approaches are reasonable.

6. **If unsure, ask.**

7. **If user asks for something contradicting a locked decision, push back once.** If user confirms, proceed and append to Section 4.

8. **End of every block, do all four of these:**
   - Prompt user to update Section 7 (STATUS)
   - **Append to Resumepoints.md** — new technical decisions, numbers, bullet
     candidates, skills demonstrated. The file is private (gitignored) and
     grows continuously. Day 3 picks final CV bullets from it. Skipping this
     means recovering the points later from memory — much worse.
   - **Remind user to `git add`, `git commit`, `git push`** — non-negotiable.
     Resumepoints.md is gitignored so it won't be in the commit, but every
     other change should be.
   - If user hasn't pushed in last 90 min, remind mid-block.


9. **Stay calibrated.** 2nd-year strong undergrad — don't condescend, don't over-explain CS fundamentals, do explain microstructure and finance concepts.

10. **Don't pad responses.** No "Great question!" preambles, no recap. Be direct.

11. **When the user flags a comprehension gap, treat it as the highest-priority next action.** Defensibility of code the user can't explain is zero. "I just copy-pasted this" → next session starts with a walkthrough of that code, no exceptions.

12.12. **Resumepoints.md is updated at the close of every block, no exceptions.**
    It is the running record of every technical decision worth defending and
    every metric worth quoting. Structure of each appended block:
      - Headline numbers (if any new ones measured this block)
      - Technical decisions made and their justifications
      - Skills demonstrated (with concrete details, not generic phrases)
      - Bullet candidates if a CV-worthy claim emerged
    The file lives at repo root, is gitignored, and survives across sessions
    via the local filesystem. If the user has just done meaningful work and

---

## 10. When to escalate back to System A (Pro Claude)

Return to System A for:
- Genuinely tricky design questions not covered in this doc
- Math derivations beyond Kalman update (e.g., RTS smoother, EM derivation)
- Strategic decisions ("should I attempt EM stretch or polish")
- Anything that might contradict locked design

When user mentions escalating, acknowledge and let them switch.

---

## 11. Git cheatsheet

**Daily flow:**

```
git pull               # start of session
git add <files>
git commit -m "..."
git push               # end of block
```

**git status** is the universal first move when confused.

**Forgot to pull, push rejected:**
```
git pull --rebase
git push
```

If conflict, edit the file, then:
```
git add <file>
git rebase --continue
git push
```

**Undo last commit (not yet pushed):**
```
git reset --soft HEAD~1   # keep changes staged
git reset HEAD~1          # keep changes unstaged
git reset --hard HEAD~1   # DESTRUCTIVE — nuke changes
```

**Switch machines with uncommitted changes:**
```
git stash; git push       # save and sync committed
# ...switch...
git stash pop             # restore
```

(Stash is local; to truly sync, commit as `WIP: ...` and clean up later.)

**VS Code Source Control markers:** `U` untracked, `M` modified, `A` added, `D` deleted.

**Rules:**
- `git status` if confused.
- Push before switching machines. Always.
- Push at end of every block. Always.
- Never `git push --force` here.

---

## 12. Open questions / TODOs

- **Sample size for analysis CSV:** Default is 100K events. May bump to 500K-1M if statistical tests need more samples. Decide on Day 2 Block 7.
- **Synthetic generator parameter values:** σ=1 (true-price step), σ=10 (limit offset), exponential rate 0.02 (mean ≈50, clipped [1, 500]) for quantity, 1µs mean inter-arrival. May need empirical tweak if Day 2 plots look off.

Add new items here as they arise. Remove when resolved.

---

## 13. Resume bullet (target)

To be finalized on Day 3 with all numbers. Working draft (Day 1 portion):

> Built a C++17 limit order book matching engine sustaining 3.5M events/sec (p50 200ns, p99 1µs, p99.9 2.2µs) on synthetic microstructure-realistic flow; ...

Day 2 (Kalman) portion to be appended once metrics are computed.

Full menu of bullet candidates lives in private `Resumepoints.md` (gitignored).

---

END OF CONTEXT.md 