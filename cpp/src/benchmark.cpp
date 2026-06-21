#include "EventGenerator.h"
#include "OrderBook.h"
#include "Order.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// benchmark — measure throughput and per-event latency of the order book.
//
// Live generation (no pre-gen) so the cancel-tracker stays in sync with
// book state. We exclude gen.next() from per-event timing by bracketing
// only the book operation with timer markers.
//
// We report two throughput figures:
//   * book-only:  inverse of the sum of measured per-event latencies
//   * wall:       total wall time / event count (includes gen.next overhead)
//
// CLI flags:
//   --events N    events to time (default 1,000,000)
//   --warmup W    warmup events before timing (default 100,000)
//   --seed S      RNG seed (default 42)
// ---------------------------------------------------------------------------

struct Args {
    uint64_t n_events = 1000000;
    uint64_t n_warmup = 100000;
    uint64_t seed     = 42;
};

static Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--events" && i + 1 < argc) {
            a.n_events = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--warmup" && i + 1 < argc) {
            a.n_warmup = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--seed" && i + 1 < argc) {
            a.seed = std::strtoull(argv[++i], nullptr, 10);
        } else {
            std::fprintf(stderr, "Unknown arg: %s\n", arg.c_str());
            std::exit(1);
        }
    }
    return a;
}

static inline void processEvent(OrderBook& book, EventGenerator& gen, const Event& ev) {
    switch (ev.type) {
        case EventType::ADD_LIMIT: {
            uint64_t initial_qty = ev.order.quantity;
            uint64_t id = book.submitLimitOrder(ev.order);
            uint64_t filled = 0;
            for (const Trade& t : book.lastTrades()) filled += t.quantity;
            if (filled < initial_qty) gen.registerResting(id);
            for (const Trade& t : book.lastTrades()) {
                if (!book.isResting(t.passiveId)) gen.unregisterResting(t.passiveId);
            }
            break;
        }
        case EventType::ADD_MARKET: {
            book.submitMarketOrder(ev.order);
            for (const Trade& t : book.lastTrades()) {
                if (!book.isResting(t.passiveId)) gen.unregisterResting(t.passiveId);
            }
            break;
        }
        case EventType::CANCEL: {
            bool ok = book.cancelOrder(ev.cancel_id);
            if (ok) gen.unregisterResting(ev.cancel_id);
            break;
        }
    }
}

int main(int argc, char** argv) {
    Args args = parseArgs(argc, argv);

    const int64_t INITIAL_TRUE_PRICE = 15000;

    OrderBook book;
    EventGenerator gen(args.seed, INITIAL_TRUE_PRICE);

    // Warmup — fill the book to steady-state depth. Not timed.
    std::printf("Warmup: %llu events...\n", (unsigned long long)args.n_warmup);
    for (uint64_t i = 0; i < args.n_warmup; ++i) {
        Event ev = gen.next();
        processEvent(book, gen, ev);
    }
    std::printf("  Book after warmup: %zu resting orders\n", book.restingOrderCount());

    // Timed loop. gen.next() runs inside the loop but OUTSIDE the per-event
    // timing brackets so its RNG cost is not attributed to book latency.
    std::vector<uint64_t> latencies_ns;
    latencies_ns.reserve(args.n_events);

    std::printf("Timing %llu events...\n", (unsigned long long)args.n_events);
    auto t_loop_start = std::chrono::high_resolution_clock::now();

    for (uint64_t i = 0; i < args.n_events; ++i) {
        Event ev = gen.next();   // NOT timed
        auto t0 = std::chrono::high_resolution_clock::now();
        processEvent(book, gen, ev);
        auto t1 = std::chrono::high_resolution_clock::now();
        uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        latencies_ns.push_back(ns);
    }

    auto t_loop_end = std::chrono::high_resolution_clock::now();
    double wall_seconds = std::chrono::duration<double>(t_loop_end - t_loop_start).count();

    // Sum of per-event latencies (book operations only).
    uint64_t book_only_ns_total = 0;
    for (uint64_t ns : latencies_ns) book_only_ns_total += ns;
    double book_only_seconds = book_only_ns_total / 1e9;

    // Percentiles.
    std::sort(latencies_ns.begin(), latencies_ns.end());

    auto pct = [&](double p) -> uint64_t {
        size_t idx = static_cast<size_t>(p * (latencies_ns.size() - 1));
        return latencies_ns[idx];
    };

    uint64_t p50   = pct(0.50);
    uint64_t p90   = pct(0.90);
    uint64_t p99   = pct(0.99);
    uint64_t p999  = pct(0.999);
    uint64_t p9999 = pct(0.9999);
    uint64_t pmax  = latencies_ns.back();

    double throughput_book_meps = (static_cast<double>(args.n_events) / book_only_seconds) / 1e6;
    double throughput_wall_meps = (static_cast<double>(args.n_events) / wall_seconds) / 1e6;

    std::printf("\n========== Benchmark results ==========\n");
    std::printf("Events timed:           %llu\n", (unsigned long long)args.n_events);
    std::printf("Final resting orders:   %zu\n", book.restingOrderCount());
    std::printf("Wall time:              %.3f s\n", wall_seconds);
    std::printf("Book-only time (sum):   %.3f s\n", book_only_seconds);
    std::printf("\n");
    std::printf("Throughput (book only): %.2f M events/sec\n", throughput_book_meps);
    std::printf("Throughput (wall):      %.2f M events/sec\n", throughput_wall_meps);
    std::printf("\n");
    std::printf("Per-event latency (ns):\n");
    std::printf("  p50    = %6llu\n", (unsigned long long)p50);
    std::printf("  p90    = %6llu\n", (unsigned long long)p90);
    std::printf("  p99    = %6llu\n", (unsigned long long)p99);
    std::printf("  p99.9  = %6llu\n", (unsigned long long)p999);
    std::printf("  p99.99 = %6llu\n", (unsigned long long)p9999);
    std::printf("  max    = %6llu\n", (unsigned long long)pmax);

    // Latency samples for the notebook histogram.
    FILE* histfile = std::fopen("benchmark_latencies.csv", "w");
    if (histfile) {
        std::fprintf(histfile, "latency_ns\n");
        for (uint64_t ns : latencies_ns) {
            std::fprintf(histfile, "%llu\n", (unsigned long long)ns);
        }
        std::fclose(histfile);
        std::printf("\nLatency samples written to benchmark_latencies.csv\n");
    }

    return 0;
}