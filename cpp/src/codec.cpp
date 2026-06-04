#include "ccsds/codec.hpp"

// Implementation lands in Step 5+. Keeping a translation unit so the static
// library is non-empty under strict toolchains.
namespace ccsds {
namespace {
[[maybe_unused]] constexpr int kBuildAnchor = scaffolding_sentinel();
}
}  // namespace ccsds
