#include "../../libraries/AP_HobbywingV5/Validity.h"
#include <cassert>
#include <cmath>
int main()
{
    assert(HobbywingV5::valid_byte_sensor(0));
    assert(HobbywingV5::valid_byte_sensor(254));
    assert(!HobbywingV5::valid_byte_sensor(255));
    assert(HobbywingV5::valid_word_sensor(0));
    assert(!HobbywingV5::valid_word_sensor(65535));
    assert(HobbywingV5::mechanical_rpm(14000, 14) == 2000.0f);
    assert(HobbywingV5::mechanical_rpm(14000, 2) == 14000.0f);
    assert(HobbywingV5::mechanical_rpm(14000, 0) == 0.0f);
    return 0;
}
