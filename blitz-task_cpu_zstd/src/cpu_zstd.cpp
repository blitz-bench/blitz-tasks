#define _CRT_SECURE_NO_WARNINGS 1

#include "cpu_zstd.hpp"

#include <zstd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

extern "C" const char* CPU_ZSTD_INFO_JSON;

namespace cpu_zstd {

namespace {

constexpr std::uint64_t DEFAULT_BUDGET_MS = 2000;

constexpr int kLevel = 3;
constexpr std::size_t kBlockSize = 1 << 20;
constexpr auto kProgressInterval = std::chrono::milliseconds(50);

std::string corpus_path() {
    if (const char* env = std::getenv("BLITZ_ZSTD_CORPUS")) {
        if (env[0] != '\0') return env;
    }
#ifdef BLITZ_ZSTD_CORPUS_PATH
    return BLITZ_ZSTD_CORPUS_PATH;
#else
    return {};
#endif
}

// Reads the whole corpus file into memory. false if the path is unset, missing or empty.
bool load_corpus(const std::string& path, std::vector<std::uint8_t>& corpus) {
    if (path.empty()) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    corpus.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return !corpus.empty();
}

} // namespace

CpuZstd::CpuZstd() : timeout_ms_(DEFAULT_BUDGET_MS) {}

CpuZstd::~CpuZstd() = default;

std::string_view CpuZstd::info_json() const noexcept { return CPU_ZSTD_INFO_JSON; }

blitz::Result CpuZstd::configure(const blitz::DataConfig& cfg) {
    iterations_ = cfg.iterations;
    return BLITZ_OK;
}

blitz::Result CpuZstd::set_timeout(std::uint64_t timeout_ms) {
    timeout_ms_ = timeout_ms;
    return BLITZ_OK;
}

blitz::Result CpuZstd::run(const blitz::Callbacks& cb) {
    if (cb.on_status) cb.on_status(BLITZ_STATUS_RUNNING);
    if (cb.on_start) cb.on_start();

    auto fail = [&](blitz::Result code, const std::string& msg) {
        if (cb.on_error) cb.on_error(code, msg);
        if (cb.on_status) cb.on_status(BLITZ_STATUS_FAILED);
        return code;
    };

    if (timeout_ms_ == 0) {
        return fail(BLITZ_ERR_INVALID_CONFIG, "timeout must be > 0");
    }

    std::vector<std::uint8_t> corpus;
    if (!load_corpus(corpus_path(), corpus)) {
        return fail(BLITZ_ERR_RESOURCE,
                    "cannot read corpus (set BLITZ_ZSTD_CORPUS or build with BLITZ_ZSTD_CORPUS_PATH)");
    }

    // Split the corpus into independently compressed fixed-size blocks.
    struct Block {
        const std::uint8_t* data;
        std::size_t size;
    };
    std::vector<Block> blocks;
    for (std::size_t off = 0; off < corpus.size(); off += kBlockSize) {
        blocks.push_back({corpus.data() + off, std::min(kBlockSize, corpus.size() - off)});
    }

    const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    const std::size_t bound = ZSTD_compressBound(kBlockSize);

    // Warm-up pass; also measures the compressed size reported as the ratio.
    std::uint64_t compressed_bytes = 0;
    {
        ZSTD_CCtx* cctx = ZSTD_createCCtx();
        if (!cctx) return fail(BLITZ_ERR_RESOURCE, "ZSTD_createCCtx failed");
        struct CCtxGuard {
            ZSTD_CCtx* c;
            ~CCtxGuard() { ZSTD_freeCCtx(c); }
        } guard{cctx};
        std::vector<std::uint8_t> dst(bound);
        for (const Block& b : blocks) {
            const std::size_t sz =
                ZSTD_compressCCtx(cctx, dst.data(), dst.size(), b.data, b.size, kLevel);
            if (ZSTD_isError(sz)) {
                return fail(BLITZ_ERR_INTERNAL,
                            std::string("ZSTD_compressCCtx failed during warm-up: ") +
                                ZSTD_getErrorName(sz));
            }
            compressed_bytes += sz;
        }
    }

    std::atomic<std::uint64_t> next_block{0};
    std::atomic<std::uint64_t> bytes_processed{0};
    std::atomic<bool> failed{false};
    const std::uint64_t max_blocks = iterations_;

    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::milliseconds(timeout_ms_);

    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) {
        workers.emplace_back([&] {
            // One reused context per thread avoids per-call allocation.
            ZSTD_CCtx* cctx = ZSTD_createCCtx();
            if (!cctx) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
            std::vector<std::uint8_t> dst(bound);
            while (!failed.load(std::memory_order_relaxed) &&
                   std::chrono::steady_clock::now() < deadline) {
                const std::uint64_t n = next_block.fetch_add(1, std::memory_order_relaxed);
                if (max_blocks != 0 && n >= max_blocks) break;
                const Block& b = blocks[n % blocks.size()];
                const std::size_t sz =
                    ZSTD_compressCCtx(cctx, dst.data(), dst.size(), b.data, b.size, kLevel);
                if (ZSTD_isError(sz)) {
                    failed.store(true, std::memory_order_relaxed);
                    break;
                }
                bytes_processed.fetch_add(b.size, std::memory_order_relaxed);
            }
            ZSTD_freeCCtx(cctx);
        });
    }

    // Callbacks fire only from this thread; workers just bump counters.
    auto next_report = start + kProgressInterval;
    while (std::chrono::steady_clock::now() < deadline) {
        if (failed.load(std::memory_order_relaxed)) break;
        if (max_blocks != 0 && next_block.load(std::memory_order_relaxed) >= max_blocks) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto now = std::chrono::steady_clock::now();
        if (cb.on_progress && now >= next_report) {
            const double secs = std::chrono::duration<double>(now - start).count();
            blitz::Metric m;
            m.name = "bandwidth";
            m.value = secs > 0.0
                          ? static_cast<double>(bytes_processed.load(std::memory_order_relaxed)) /
                                secs / 1e9
                          : 0.0;
            m.unit = "GB/s";
            m.direction = BLITZ_DIR_HIGHER_IS_BETTER;
            cb.on_progress(m);
            next_report = now + kProgressInterval;
        }
    }

    for (auto& w : workers) w.join();
    if (failed.load()) {
        return fail(BLITZ_ERR_INTERNAL, "zstd compression failed in a worker thread");
    }

    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const std::uint64_t bytes = bytes_processed.load();
    const double bandwidth = secs > 0.0 ? static_cast<double>(bytes) / secs / 1e9 : 0.0;

    char ratio[32];
    std::snprintf(ratio, sizeof(ratio), "%.2f",
                  compressed_bytes > 0
                      ? static_cast<double>(corpus.size()) / static_cast<double>(compressed_bytes)
                      : 0.0);

    std::vector<blitz::Metric> metrics(1);
    metrics[0].name = "bandwidth";
    metrics[0].value = bandwidth;
    metrics[0].unit = "GB/s";
    metrics[0].direction = BLITZ_DIR_HIGHER_IS_BETTER;
    metrics[0].info = {
        {"level", std::to_string(kLevel)},
        {"threads", std::to_string(threads)},
        {"block_size", std::to_string(kBlockSize)},
        {"corpus_bytes", std::to_string(corpus.size())},
        {"compressed_bytes", std::to_string(compressed_bytes)},
        {"ratio", ratio},
        {"bytes_processed", std::to_string(bytes)},
        {"zstd_version", ZSTD_versionString()},
    };

    if (cb.on_complete) cb.on_complete(metrics);
    if (cb.on_status) cb.on_status(BLITZ_STATUS_COMPLETED);
    return BLITZ_OK;
}

} // namespace cpu_zstd

extern "C" ::BlitzTask* cpu_zstd_new(void) {
    return blitz::make_c_task(std::make_unique<cpu_zstd::CpuZstd>());
}
