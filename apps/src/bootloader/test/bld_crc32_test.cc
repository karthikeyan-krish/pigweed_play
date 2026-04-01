#include <gtest/gtest.h>

extern "C" {
#include "bld_crc32.h"
}

TEST(BldCrc32Test, EmptyBufferWithZeroSeedReturnsZero) {
  EXPECT_EQ(bld_crc32_ieee(nullptr, 0, BLD_CRC32_INITIAL), 0u);
}

TEST(BldCrc32Test, StandardVector123456789MatchesKnownCrc32) {
  static constexpr char kData[] = "123456789";
  EXPECT_EQ(bld_crc32_ieee(kData, 9, BLD_CRC32_INITIAL), 0xCBF43926u);
}

TEST(BldCrc32Test, ChunkedCalculationMatchesSinglePass) {
  static constexpr uint8_t kData[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  const uint32_t whole =
      bld_crc32_ieee(kData, sizeof(kData), BLD_CRC32_INITIAL);
  uint32_t part = bld_crc32_ieee(kData, 4, BLD_CRC32_INITIAL);
  part = bld_crc32_ieee(kData + 4, sizeof(kData) - 4, part);
  EXPECT_EQ(part, whole);
}
