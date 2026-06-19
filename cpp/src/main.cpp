#include "EventGenerator.h"
#include "OrderBook.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

// ---------------------------------------------------------------------------
// main — drives the simulation.
//
// Generates N synthetic events, feeds them to the order book, and writes two
// CSVs: midprices.csv (per-event book state) and trades.csv (every fill).
//
// CLI flags:
//   --events N         number of events to generate (default 100000)
//   --seed S           RNG seed (default 42)
//   --output DIR       output directory for CSVs (default ./)
//   --verbose          print per-event activity to stdout
// ---------------------------------------------------------------------------

struct Args {
    uint64_t    n_events = 100000;
    uint64_t    seed     = 42;
    std::string output_dir = "./";
    bool        verbose  = false;
};

static Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--events" && i + 1 < argc) {
            a.n_events = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--seed" && i + 1 < argc) {
            a.seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--output" && i + 1 < argc) {
            a.output_dir = argv[++i];
            if (!a.output_dir.empty() && a.output_dir.back() != '/' && a.output_dir.back() != '\\') {
                a.output_dir += '/';
            }
        } else if (arg == "--verbose") {
            a.verbose = true;
        } else {
            std::fprintf(stderr, "Unknown arg: %s\n", arg.c_str());
            std::exit(1);
        }
    }
    return a;
}

int main(int argc, char** argv) {
    Args args = parseArgs(argc, argv);

    constexpr int64_t INITIAL_TRUE_PRICE = 15000;  // $150.00

    OrderBook book;
    book.setVerbose(args.verbose);

    EventGenerator gen(args.seed, INITIAL_TRUE_PRICE);

    // Open CSV outputs.
    std::ofstream midfile(args.output_dir + "midprices.csv");
    std::ofstream tradefile(args.output_dir + "trades.csv");
    if (!midfile.is_open() || !tradefile.is_open()) {
        std::fprintf(stderr, "Failed to open output files in %s\n", args.output_dir.c_str());
        return 1;
    }

    // Headers.
    midfile   << "timestamp,best_bid,best_ask,mid_price,true_price\n";
    tradefile << "timestamp,aggressive_id,passive_id,price,quantity\n";

    uint64_t trades_written = 0;

    for (uint64_t i = 0; i < args.n_events; ++i) {
        Event ev = gen.next();

        switch (ev.type) {
            case EventType::ADD_LIMIT: {
                uint64_t initial_qty = ev.order.quantity;
                uint64_t id = book.submitLimitOrder(ev.order);
                // If any portion rested, register it for future cancels.
                // We can detect resting by comparing trade totals to initial qty.
                uint64_t filled = 0;
                for (const Trade& t : book.lastTrades()) filled += t.quantity;
                if (filled < initial_qty) {
                    gen.registerResting(id);
                }
                // Any fully-consumed passive orders should be unregistered.
                for (const Trade& t : book.lastTrades()) {
                    // Heuristic: if a passive order is fully consumed, it leaves
                    // the book. We track partial consumption later via cancels.
                    // For simplicity, we always try to unregister; the call is
                    // a no-op for orders still resting (they'll get hit again
                    // on a later cancel attempt).
                    if(!book.isResting(t.passiveId)){
                        gen.unregisterResting(t.passiveId);
                    }
                }
                break;
            }
            case EventType::ADD_MARKET: {
                book.submitMarketOrder(ev.order);
                // Market orders never rest; just unregister any passives fully consumed.
                for (const Trade& t : book.lastTrades()) {
                    gen.unregisterResting(t.passiveId);
                }
                break;
            }
            case EventType::CANCEL: {
                bool ok = book.cancelOrder(ev.cancel_id);
                if (ok) gen.unregisterResting(ev.cancel_id);
                break;
            }
        }

        // Write trades produced by this event.
        for (const Trade& t : book.lastTrades()) {
            tradefile << t.timestamp << ","
                      << t.aggressiveId << ","
                      << t.passiveId << ","
                      << t.price << ","
                      << t.quantity << "\n";
            ++trades_written;
        }

        // Write the mid-price snapshot.
        midfile << ev.timestamp << ","
                << book.bestBid() << ","
                << book.bestAsk() << ","
                << book.midPrice() << ","
                << gen.trueprice() << "\n";
    }

    midfile.close();
    tradefile.close();

    std::printf("Simulation complete.\n");
    std::printf("  Events processed: %llu\n", (unsigned long long)args.n_events);
    std::printf("  Trades written:   %llu\n", (unsigned long long)trades_written);
    std::printf("  Final resting:    %zu\n", book.restingOrderCount());
    std::printf("  Output:           %smidprices.csv, %strades.csv\n",
                args.output_dir.c_str(), args.output_dir.c_str());

    return 0;
}