// Copyright 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <univalue.h>
#include <vector>

static void BenchmarkUniValueRoundTrip(benchmark::Bench& bench) {
    std::string json = R"({"name":"test","value":123,"nested":{"key":"value"},"array":[1,2,3]})";
    
    bench.run([&]() {
        UniValue v;
        v.read(json);
        std::string output = v.write();
        return output;
    });
}

static void BenchmarkUniValueSimpleNumber(benchmark::Bench& bench) {
    bench.run([&]() {
        UniValue v;
        v.setInt(12345);
        std::string output = v.write();
        return output;
    });
}

static void BenchmarkUniValueSimpleString(benchmark::Bench& bench) {
    bench.run([&]() {
        UniValue v;
        v.setStr("hello world");
        std::string output = v.write();
        return output;
    });
}

static void BenchmarkUniValueArrayCreation(benchmark::Bench& bench) {
    bench.run([&]() {
        UniValue arr(UniValue::VARR);
        for (int i = 0; i < 100; ++i) {
            arr.push_back(UniValue(i));
        }
        std::string output = arr.write();
        return output;
    });
}

static void BenchmarkUniValueObjectCreation(benchmark::Bench& bench) {
    bench.run([&]() {
        UniValue obj(UniValue::VOBJ);
        for (int i = 0; i < 100; ++i) {
            obj.pushKV(std::string("key") + std::to_string(i), UniValue(i));
        }
        std::string output = obj.write();
        return output;
    });
}

BENCHMARK(BenchmarkUniValueRoundTrip);
BENCHMARK(BenchmarkUniValueSimpleNumber);
BENCHMARK(BenchmarkUniValueSimpleString);
BENCHMARK(BenchmarkUniValueArrayCreation);
BENCHMARK(BenchmarkUniValueObjectCreation);
