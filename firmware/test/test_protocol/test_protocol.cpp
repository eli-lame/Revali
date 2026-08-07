// First native test — proves the `native` PlatformIO environment actually
// works end to end (no ESP32, no hardware), and verifies the CRC16
// implementation in shared/link/protocol.h against its published test
// vector before anything in Phase 5 is built on top of it.

#include <unity.h>

#include "link/protocol.h"

using namespace revali::link;

void setUp() {}
void tearDown() {}

// CRC-16/CCITT-FALSE check value for ASCII "123456789" is the standard
// published test vector for this variant (poly 0x1021, init 0xFFFF).
void test_crc16_ccitt_check_value() {
    const uint8_t input[] = "123456789";
    uint16_t crc = crc16_ccitt(input, 9);
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc);
}

void test_crc16_ccitt_empty_input() {
    uint16_t crc = crc16_ccitt(nullptr, 0);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc);
}

void test_cmd_control_wire_size() {
    TEST_ASSERT_EQUAL(10, sizeof(CmdControl));
}

void test_tlm_state_wire_size() {
    TEST_ASSERT_EQUAL(19, sizeof(TlmState));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_crc16_ccitt_check_value);
    RUN_TEST(test_crc16_ccitt_empty_input);
    RUN_TEST(test_cmd_control_wire_size);
    RUN_TEST(test_tlm_state_wire_size);
    return UNITY_END();
}
