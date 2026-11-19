#include "../../libraries/AP_HobbywingV5/Timing.h"
#include <assert.h>
#include <stdint.h>
using namespace HobbywingV5;
int main()
{
    Timing t;
    assert(!t.has_sample() && !t.fresh(0));
    assert(t.age_us(0)==UINT32_MAX && t.frequency_hz_x10()==0);
    t.mark_valid(1000000);
    assert(t.count()==1 && t.interval_count()==0 && t.fresh(1000000));
    for (unsigned i=1;i<=20;i++) t.mark_valid(1000000 + i*100000);
    assert(t.interval_us()==100000 && t.max_interval_us()==100000);
    assert(t.mean_interval_us()==100000 && t.frequency_hz_x10()==100);
    assert(t.age_us(3100000)==100000 && t.fresh(3100000));
    assert(!t.fresh(3600001));
    Timing wrap;
    wrap.mark_valid(UINT32_MAX - 50000);
    wrap.mark_valid(49999); // difference 100000us across wrap
    assert(wrap.interval_us()==100000);
    assert(wrap.fresh(50000));
    return 0;
}
