/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <AP_gtest.h>
#include "../AP_SpektrumSmartESC_Receive.h"
#include <vector>

const AP_HAL::HAL &hal = AP_HAL::get_HAL();

class MockReceiver : public AP_HAL::UARTDriver {
public:
    MockReceiver() { lock_write_key = lock_read_key = 0; parity = 0; _last_options = 0; }
    struct Byte { uint8_t byte; uint32_t ms; };
    std::vector<Byte> input;
    size_t offset = 0;
    uint32_t writes = 0;
    uint32_t faults = 0;
    uint32_t begins = 0;
    bool initialized = false;
    bool receive_only = true, leased = false;
    bool receive_diagnostics(ReceiveDiagnostics &state) const override {
        state = {}; state.handshake_flags = leased ? 2 : 0; return true;
    }
    bool is_receive_only() const override { return receive_only; }
    bool is_initialized() override { return initialized; }
    bool tx_pending() override { return false; }
    uint32_t txspace() override { return 0; }
    uint32_t receive_errors() const override { return faults; }
    bool read_timed(uint8_t &value, uint32_t &ms) override {
        if (offset == input.size()) { return false; }
        value = input[offset].byte;
        ms = input[offset++].ms;
        return true;
    }
    void packet(uint8_t *p, uint32_t ms) {
        for (uint8_t i = 0; i < p[2]; i++) { input.push_back({p[i], ms}); }
    }
protected:
    void _begin(uint32_t baud, uint16_t, uint16_t) override { begins++; initialized = baud == 115200; }
    void _end() override { initialized = false; }
    void _flush() override {}
    size_t _write(const uint8_t *, size_t) override { writes++; return 0; }
    ssize_t _read(uint8_t *, uint16_t) override { return 0; }
    uint32_t _available() override { return input.size() - offset; }
    bool _discard_input() override { offset = input.size(); return true; }
};

static void seal(uint8_t *p)
{
    const uint16_t crc = AP_SpektrumSmartESC::crc16(p, p[2] - 2);
    p[p[2] - 2] = crc >> 8;
    p[p[2] - 1] = crc;
}

static void handshake(MockReceiver &source, uint32_t ms)
{
    uint8_t p[] = {0xA6, 0x21, 14, 0x40, 0x30, 10, 0, 4, 1, 2, 3, 4, 0, 0};
    seal(p);
    source.packet(p, ms);
    p[3] = 0x30;
    p[4] = 0xFF;
    seal(p);
    source.packet(p, ms);
}

static void telemetry(MockReceiver &source, uint32_t ms, bool missing_temperature = false)
{
    uint8_t poll[] = {0xA6, 0xCD, 16, 0, 0x40, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
    seal(poll);
    source.packet(poll, ms);
    uint8_t p[] = {0xA6, 0x80, 22, 0x30, 0x20, 0, 0x04, 0xD2, 0x09, 0x60,
                   0x02, 0x8A, 0x04, 0xD2, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0};
    if (missing_temperature) { p[10] = p[11] = 0xFF; }
    seal(p);
    source.packet(p, ms);
}

TEST(SpektrumReceive, RefusesWritableOrAlreadyOwnedPort)
{
    MockReceiver source;
    AP_SpektrumSmartESC_Receive receiver(source);
    source.receive_only = false;
    EXPECT_FALSE(receiver.start());
    EXPECT_EQ(source.begins, 0U);
    source.receive_only = true;
    source.initialized = true;
    EXPECT_FALSE(receiver.start());
    EXPECT_EQ(source.begins, 0U);
    EXPECT_EQ(source.writes, 0U);
}

TEST(SpektrumReceive, ObservationAndStopNeverWrite)
{
    MockReceiver source;
    AP_SpektrumSmartESC_Receive receiver(source);
    EXPECT_TRUE(receiver.start());
    handshake(source, 0);
    telemetry(source, 1);
    receiver.update(2);
    AP_SpektrumSmartESC::Telemetry data {};
    EXPECT_TRUE(receiver.parser().telemetry(data, 2));
    EXPECT_EQ(receiver.bytes(), 66U);
    EXPECT_EQ(receiver.last_byte_ms(), 1U);
    receiver.stop();
    EXPECT_FALSE(source.initialized);
    EXPECT_FALSE(receiver.parser().telemetry(data, 2));
    EXPECT_EQ(source.writes, 0U);
}

TEST(SpektrumReceive, PreservesAcquisitionTimesAcrossSlowVehicleUpdates)
{
    MockReceiver source;
    AP_SpektrumSmartESC_Receive receiver(source);
    EXPECT_TRUE(receiver.start());
    handshake(source, 0);
    auto rest = std::vector<MockReceiver::Byte>(source.input.begin() + 7, source.input.end());
    source.input.resize(7);
    receiver.update(0);
    for (auto byte : rest) { byte.ms = 1; source.input.push_back(byte); }
    receiver.update(10);
    AP_SpektrumSmartESC::Device device {};
    EXPECT_TRUE(receiver.parser().device(device, 10));
    EXPECT_EQ(receiver.parser().counters().incomplete, 0U);
}

TEST(SpektrumReceive, OldBacklogCannotBecomeFresh)
{
    MockReceiver source;
    AP_SpektrumSmartESC_Receive receiver(source);
    EXPECT_TRUE(receiver.start());
    handshake(source, 0);
    telemetry(source, 1);
    receiver.update(1000);
    AP_SpektrumSmartESC::Telemetry data {};
    EXPECT_FALSE(receiver.parser().telemetry(data, 1000));
}

#if HAL_WITH_ESC_TELEM
static AP_ESC_Telem &frontend()
{
    static AP_ESC_Telem instance;
    instance.invalidate_telem_data(14);
    return instance;
}

TEST(SpektrumReceive, FreshPublicationTimeoutAndRecovery)
{
    auto &esc = frontend();
    MockReceiver source;
    AP_SpektrumSmartESC_Receive receiver(source);
    EXPECT_TRUE(receiver.start());
    EXPECT_TRUE(receiver.set_frontend(esc, 14));
    handshake(source, 0);
    telemetry(source, 1);
    receiver.update(2);
    const auto count = esc.get_telem_data(14).count;
    EXPECT_NEAR(esc.get_telem_data(14).current, 12.34f, 0.001f);
    receiver.update(3);
    EXPECT_EQ(esc.get_telem_data(14).count, count);
    receiver.update(51);
    EXPECT_EQ(esc.get_telem_data(14).types, 0);
    telemetry(source, 52);
    receiver.update(52);
    EXPECT_EQ(esc.get_telem_data(14).count, count);
    handshake(source, 53);
    telemetry(source, 54);
    receiver.update(55);
    EXPECT_EQ(esc.get_telem_data(14).count, uint16_t(count + 1));
    source.faults++;
    receiver.update(56);
    EXPECT_EQ(receiver.errors(), 1U);
    EXPECT_EQ(esc.get_telem_data(14).types, 0);
    EXPECT_EQ(source.writes, 0U);
}

TEST(SpektrumReceive, MissingFieldsWithdrawEarlierSnapshot)
{
    auto &esc = frontend();
    MockReceiver source;
    AP_SpektrumSmartESC_Receive receiver(source);
    EXPECT_TRUE(receiver.start());
    EXPECT_TRUE(receiver.set_frontend(esc, 14));
    handshake(source, 0);
    telemetry(source, 1);
    receiver.update(2);
    EXPECT_NE(esc.get_telem_data(14).types, 0);
    telemetry(source, 3, true);
    receiver.update(4);
    EXPECT_EQ(esc.get_telem_data(14).types, 0);
    EXPECT_EQ(esc.get_last_telem_data_ms(14), 0U);
}

TEST(SpektrumReceive, ConflictingProducerIsNotOverwritten)
{
    auto &esc = frontend();
    MockReceiver source;
    AP_SpektrumSmartESC_Receive receiver(source);
    EXPECT_TRUE(receiver.start());
    EXPECT_TRUE(receiver.set_frontend(esc, 14));
    AP_ESC_Telem_Backend::TelemetryData other {};
    other.voltage = 42;
    esc.update_telem_data(14, other, AP_ESC_Telem_Backend::VOLTAGE);
    handshake(source, 0);
    telemetry(source, 1);
    receiver.update(2);
    EXPECT_TRUE(receiver.frontend_conflict());
    receiver.stop();
    EXPECT_NEAR(esc.get_telem_data(14).voltage, 42.0f, 0.001f);
    EXPECT_EQ(esc.get_telem_data(14).types, AP_ESC_Telem_Backend::VOLTAGE);
    EXPECT_EQ(source.writes, 0U);
}

TEST(SpektrumReceive, NativeLeasePublishesCompleteFreshSnapshotsOnly)
{
    auto &esc=frontend();
    MockReceiver source;
    source.initialized=true; source.receive_only=false; source.leased=true;
    AP_SpektrumSmartESC_Receive receiver(source);
    EXPECT_TRUE(receiver.set_frontend(esc,14));
    AP_SpektrumSmartESC::Telemetry data;
    data.valid=AP_SpektrumSmartESC::VOLTAGE|AP_SpektrumSmartESC::CURRENT|AP_SpektrumSmartESC::TEMPERATURE;
    data.voltage=12; data.current=3; data.temperature_c=30;
    EXPECT_TRUE(receiver.publish_native(data,100,120));
    EXPECT_NE(esc.get_telem_data(14).types,0);
    data.valid=AP_SpektrumSmartESC::VOLTAGE;
    EXPECT_FALSE(receiver.publish_native(data,121,122));
    EXPECT_EQ(esc.get_telem_data(14).types,0); // Missing fields cannot stay fresh.
    data.valid=AP_SpektrumSmartESC::VOLTAGE|AP_SpektrumSmartESC::CURRENT|AP_SpektrumSmartESC::TEMPERATURE;
    EXPECT_TRUE(receiver.publish_native(data,130,131));
    EXPECT_FALSE(receiver.publish_native(data,130,180));
    EXPECT_EQ(esc.get_telem_data(14).types,0);
    EXPECT_EQ(source.writes,0U);
}
#endif

TEST(SpektrumReceive, NativeShutdownStopsObservation)
{
    MockReceiver source;
    AP_SpektrumSmartESC_Receive receiver(source);
    EXPECT_TRUE(receiver.start());
    handshake(source, 1);
    telemetry(source, 2);
    receiver.update(2);
    AP_SpektrumSmartESC::Telemetry data;
    EXPECT_TRUE(receiver.parser().telemetry(data, 2));
    source.initialized = false; // Native register guard disconnected the pin.
    source.faults++;
    receiver.update(3);
    EXPECT_FALSE(receiver.running());
    EXPECT_FALSE(receiver.parser().telemetry(data, 3));
    EXPECT_EQ(source.writes, 0U);
}

TEST(SpektrumReceive, SuspendResumePreservesPortButClearsSession)
{
    MockReceiver source;
    AP_SpektrumSmartESC_Receive receiver(source);
    EXPECT_TRUE(receiver.start()); handshake(source, 1); receiver.update(1);
    receiver.suspend(); EXPECT_FALSE(receiver.running()); EXPECT_TRUE(source.initialized);
    AP_SpektrumSmartESC::Device d {}; EXPECT_FALSE(receiver.parser().device(d, 1));
    source.leased = true; EXPECT_FALSE(receiver.resume());
    source.leased = false; EXPECT_TRUE(receiver.resume()); EXPECT_EQ(source.begins, 1U);
    EXPECT_FALSE(receiver.parser().device(d, 1));
    receiver.stop(); EXPECT_FALSE(receiver.resume()); EXPECT_FALSE(receiver.running());
    EXPECT_EQ(source.writes, 0U);
}

AP_GTEST_MAIN()
