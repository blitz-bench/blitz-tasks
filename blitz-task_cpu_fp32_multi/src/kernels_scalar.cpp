// Built with no ISA flags - the plain baseline. See BlitzKernelTiers.cmake.
#include <platform.h>

#include <synthetic_ops.h>

#include "kernels.hpp"

namespace cpu_fp_multi {

// Scalar fp32 add: one lane per chain, accumulators in xmm registers.
BLITZBENCH_ADD_KERNEL(fp32_scalar, float, 1.0f, a + b, 1)

} // namespace cpu_fp_multi
