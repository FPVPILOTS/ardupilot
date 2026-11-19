#include "../../libraries/AP_HobbywingV5/CurrentCalibration.h"
#include <cassert>
#include <cmath>
int main()
{
    using HobbywingV5::corrected_current;
    using HobbywingV5::current_above_pm_threshold;
    assert(corrected_current(2.9f, 1.0f, 0.0f, 39) > 2.89f);
    assert(corrected_current(10.1f, 1.5f, 2.0f, 50) > 17.14f);
    assert(corrected_current(10.1f, 1.5f, 2.0f, 50) < 17.16f);
    assert(corrected_current(0.0f, 1.5f, 2.0f, 50) == 0.0f);
    assert(corrected_current(10.0f, 1.5f, 2.0f, 0) == 0.0f);
    assert(corrected_current(2.0f, 1.0f, -5.0f, 20) == 0.0f);
    assert(corrected_current(-5.0f, 1.0f, 0.0f, 20) == 0.0f);
    assert(corrected_current(NAN, 1.0f, 0.0f, 20) == 0.0f);
    assert(corrected_current(2.0f, 0.0f, 0.0f, 20) == 0.0f);
    assert(corrected_current(2.0f, 1.0f, NAN, 20) == 0.0f);
    assert(corrected_current(999.0f, 10.0f, 0.0f, 20) == 1000.0f);
    assert(current_above_pm_threshold(0.0f, false, 0.0f));
    assert(!current_above_pm_threshold(5.0f, false, 10.0f));
    assert(!current_above_pm_threshold(5.0f, true, 4.9f));
    assert(current_above_pm_threshold(5.0f, true, 5.0f));
    assert(current_above_pm_threshold(5.0f, true, 6.4f));
    assert(!current_above_pm_threshold(5.0f, true, NAN));
    return 0;
}
