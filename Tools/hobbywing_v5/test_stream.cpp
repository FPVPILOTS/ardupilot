#include "../../libraries/AP_HobbywingV5/Stream.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

using namespace HobbywingV5;
static void put16(uint8_t *p, uint16_t v) { p[0]=uint8_t(v); p[1]=uint8_t(v>>8); }
static void good(uint8_t *p, uint8_t throttle)
{
    for (unsigned i=0;i<32;i++) p[i]=0;
    p[0]=0xFD; p[1]=1; p[3]=3; put16(p+4,0x5C30);
    p[9]=throttle; put16(p+13,1200); put16(p+15,251); put16(p+17,312);
    put16(p+30,crc16_modbus(p,30));
}
int main()
{
    Stream s;
    Sample o{};
    uint8_t packet[32]; good(packet,42);
    // Random bytes prior to valid frame should be skipped.
    for (unsigned i=0;i<70;i++) assert(!s.feed(0x7F,o));
    for (unsigned i=0;i<31;i++) assert(!s.feed(packet[i],o));
    assert(s.feed(packet[31],o));
    assert(o.throttle_percent==42 && s.valid_count()==1);

    // Truncated packet then a new packet: decoder must regain synchronization.
    for (unsigned i=0;i<14;i++) assert(!s.feed(packet[i],o));
    good(packet,71);
    bool recovered=false;
    for (unsigned i=0;i<32;i++) recovered |= s.feed(packet[i],o);
    assert(recovered && o.throttle_percent==71 && s.valid_count()==2);
    assert(s.invalid_count()>=1);

    // One bad CRC frame followed by a clean one.
    good(packet,60); packet[17]^=0x11;
    for (unsigned i=0;i<32;i++) assert(!s.feed(packet[i],o));
    good(packet,88);
    recovered=false;
    for (unsigned i=0;i<32;i++) recovered |= s.feed(packet[i],o);
    assert(recovered && o.throttle_percent==88);
    assert(s.valid_count()==3);
    return 0;
}
