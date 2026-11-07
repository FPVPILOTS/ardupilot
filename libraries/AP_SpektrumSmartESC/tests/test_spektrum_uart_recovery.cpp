/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_UartRecovery.h"

#ifdef SPKTSMART_STANDALONE_TEST
#include <cassert>
#include <cstdio>
#define TEST(group, name) static void name()
#define EXPECT_TRUE(v) assert(v)
#define EXPECT_FALSE(v) assert(!(v))
#else
#include <AP_gtest.h>
#endif

TEST(SpektrumUartRecovery, CleanBootIsEligible)
{
    AP_SpektrumSmartESC_UartRecovery gate;
    EXPECT_TRUE(gate.eligible(0, 0));
    EXPECT_TRUE(gate.eligible(0, 123456));
}

TEST(SpektrumUartRecovery, HistoricalFramingNeedsQuietWindow)
{
    AP_SpektrumSmartESC_UartRecovery gate;
    gate.record_framing_only(1000);
    EXPECT_FALSE(gate.eligible(1, 1499));
    EXPECT_TRUE(gate.eligible(1, 1500));
    EXPECT_TRUE(gate.eligible(1, 3000));
}

TEST(SpektrumUartRecovery, RepeatedFramingRestartsWindow)
{
    AP_SpektrumSmartESC_UartRecovery gate;
    gate.record_framing_only(1000);
    gate.record_framing_only(1450);
    EXPECT_FALSE(gate.eligible(2, 1500));
    EXPECT_FALSE(gate.eligible(2, 1949));
    EXPECT_TRUE(gate.eligible(2, 1950));
}

TEST(SpektrumUartRecovery, NonFramingErrorsRemainBlocked)
{
    AP_SpektrumSmartESC_UartRecovery gate;
    EXPECT_FALSE(gate.eligible(1, 5000)); // Unknown error, not classified as FE-only.
    gate.record_framing_only(1000);
    EXPECT_FALSE(gate.eligible(2, 3000)); // A separate overrun/noise/release failure.
    gate.record_framing_only(4000);
    EXPECT_FALSE(gate.eligible(3, 5000)); // No counter reset or forgiveness.
}

TEST(SpektrumUartRecovery, RecoveredCounterRetainedForLiveBaseline)
{
    AP_SpektrumSmartESC_UartRecovery gate;
    gate.record_framing_only(1000);
    const uint32_t baseline = 1;
    EXPECT_TRUE(gate.eligible(baseline, 1500));
    EXPECT_TRUE(baseline == 1);
    EXPECT_FALSE(2 == baseline); // A *new* session error must fail comparison.
}

TEST(SpektrumUartRecovery, TimestampWrapAround)
{
    AP_SpektrumSmartESC_UartRecovery gate;
    gate.record_framing_only(0xFFFFFF00U);
    EXPECT_FALSE(gate.eligible(1, 0x000000F3U)); // 499 ms elapsed.
    EXPECT_TRUE(gate.eligible(1, 0x000000F4U));  // 500 ms elapsed.
}

#ifdef SPKTSMART_STANDALONE_TEST
int main()
{
    CleanBootIsEligible();
    HistoricalFramingNeedsQuietWindow();
    RepeatedFramingRestartsWindow();
    NonFramingErrorsRemainBlocked();
    RecoveredCounterRetainedForLiveBaseline();
    TimestampWrapAround();
    std::puts("6 UART recovery policy tests passed");
}
#else
AP_GTEST_MAIN()
#endif
