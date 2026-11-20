
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

#include "Exchange.hpp"

constexpr std::uint64_t TOTAL_ORDERS = 100000;
constexpr int ITERATIONS = 5;

struct BenchmarkResult
{
    double time;
    double throughput;
    std::size_t trades;
    std::uint64_t accepted;
};

BenchmarkResult run_benchmark()
{
    Exchange exchange;
    std::uint64_t accepted = 0;

    auto start = std::chrono::steady_clock::now();

    for (std::uint64_t i = 1; i <= TOTAL_ORDERS; ++i)
    {
        Order order{
            i,
            "AAPL",
            i % 2 == 0 ? Side::BUY : Side::SELL,
            15000,
            10
        };

        if (exchange.add_order(order))
            ++accepted;
    }

    auto end = std::chrono::steady_clock::now();

    double elapsed =
        std::chrono::duration<double>(end - start).count();

    return {
        elapsed,
        accepted / elapsed,
        exchange.get_trades("AAPL").size(),
        accepted
    };
}

int main()
{
    std::vector<double> times;
    std::vector<double> throughputs;

    std::uint64_t total_trades = 0;

    // Warm-up run
    run_benchmark();

    std::cout << "\n===== Trading Engine Benchmark =====\n";

    for (int i = 1; i <= ITERATIONS; ++i)
    {
        auto result = run_benchmark();

        times.push_back(result.time);
        throughputs.push_back(result.throughput);
        total_trades += result.trades;

        std::cout << "Run " << i
                  << " | Time: " << result.time
                  << "s | Throughput: "
                  << result.throughput / 1'000'000
                  << " million orders/sec\n";
    }

    double average_time = 0;
    double average_throughput = 0;

    for (double value : times)
        average_time += value;

    for (double value : throughputs)
        average_throughput += value;

    average_time /= ITERATIONS;
    average_throughput /= ITERATIONS;

    std::sort(times.begin(), times.end());
    std::sort(throughputs.begin(), throughputs.end());

    std::cout << std::fixed << std::setprecision(4);

    std::cout << "\n===== Benchmark Summary =====\n";
    std::cout << "Orders per run: " << TOTAL_ORDERS << '\n';
    std::cout << "Iterations: " << ITERATIONS << '\n';
    std::cout << "Average Time: " << average_time << " seconds\n";
    std::cout << "Median Time: " << times[ITERATIONS / 2] << " seconds\n";
    std::cout << "Average Throughput: "
              << average_throughput / 1'000'000
              << " million orders/sec\n";
    std::cout << "Median Throughput: "
              << throughputs[ITERATIONS / 2] / 1'000'000
              << " million orders/sec\n";
    std::cout << "Trades per run: "
              << total_trades / ITERATIONS << '\n';
}
