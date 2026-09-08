#pragma once

#include <blitz_task.h>

#include <blitz_task.hpp>
#include <cstdint>

namespace cpu_zstd {

/**
 * @class CpuZstd
 * @brief Zstandard compression throughput.
 *
 * Compresses a bundled data corpus with zstd at a fixed level; one worker
 * thread per core runs independent single-shot block compressions. Reported
 * in GB/s of uncompressed input processed.
 */
class CPP_TASK_DEMO_EXPORT CpuZstd : public blitz::Task {
 public:
  CpuZstd();
  ~CpuZstd() override;

  [[nodiscard]] std::string_view info_json() const noexcept override;
  blitz::Result configure(const blitz::DataConfig& cfg) override;
  blitz::Result set_timeout(std::uint64_t timeout_ms) override;
  blitz::Result run(const blitz::Callbacks& cb) override;

 private:
  std::uint64_t timeout_ms_;
  std::uint64_t iterations_{0};
};

}  // namespace cpu_zstd

extern "C" {
CPP_TASK_DEMO_EXPORT BlitzTask* cpu_zstd_new(void);
}
