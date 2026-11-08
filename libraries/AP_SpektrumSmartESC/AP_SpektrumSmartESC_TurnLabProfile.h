/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC_TurnLab.h"

// Reviewed source-only qualification point. NOT AP_Params. No default Avian
// endpoint or direction polarity: the programmer CH7 setting does not supply
// wire values. See AVIAN130A_PROFILE_AND_STARTUP_REQUIREMENTS.md.
// Before changing this, independently confirm CH1 neutral/full, CH7 forward
// polarity, external electrical evidence, ESC loss/stop behavior, and the
// deliberately installed matching IO image CRC. Reverse endpoint qualification
// is reserved for the normal reversible motor controller, not this forward-only
// TurnLab. Never auto-install IO here.
inline AP_SpektrumSmartESC_TurnLab::Profile spektrum_turnlab_profile()
{
    return {}; // UNQUALIFIED: all physical channel updates are refused.
}
