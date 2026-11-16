#!/usr/bin/env python3
"""Static regression checks for the Pixhawk4 KST 560 Hz port."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    rcoutput = (ROOT / "libraries/AP_HAL_ChibiOS/RCOutput.cpp").read_text()
    iomcu_fw = (ROOT / "libraries/AP_IOMCU/iofirmware/iofirmware.cpp").read_text()
    iomcu_fmu = (ROOT / "libraries/AP_IOMCU/AP_IOMCU.cpp").read_text()
    iomcu_header = (ROOT / "libraries/AP_IOMCU/AP_IOMCU.h").read_text()
    parameters = (ROOT / "ArduCopter/Parameters.cpp").read_text()
    srv_parameters = (ROOT / "libraries/SRV_Channel/SRV_Channel.cpp").read_text()
    srv_output = (ROOT / "libraries/SRV_Channel/SRV_Channel_aux.cpp").read_text()
    swash = (ROOT / "libraries/AP_Motors/AP_MotorsHeli_Swash.cpp").read_text()
    heli_single = (ROOT / "libraries/AP_Motors/AP_MotorsHeli_Single.cpp").read_text()
    heli_dual = (ROOT / "libraries/AP_Motors/AP_MotorsHeli_Dual.cpp").read_text()
    heli_motors = (ROOT / "libraries/AP_Motors/AP_MotorsHeli_Single.cpp").read_text()
    rpm_config = (ROOT / "libraries/AP_RPM/AP_RPM_config.h").read_text()
    rpm_source = (ROOT / "libraries/AP_RPM/AP_RPM.cpp").read_text()
    rpm_params = (ROOT / "libraries/AP_RPM/AP_RPM_Params.cpp").read_text()
    board_readme = (ROOT / "libraries/AP_HAL_ChibiOS/hwdef/Pixhawk4/README.md").read_text()

    require("PWM 1 and 2 in group1" in board_readme and
            "PWM 3 and 4 in group2" in board_readme and
            "PWM 5, 6, 7 and 8 in group3" in board_readme,
            "Pixhawk4 MAIN timer group layout changed")
    require("#if defined(IOMCU_FW)" in rcoutput and
            "freq_hz == 560" in rcoutput and
            "(group.ch_mask & ~0x0FU) == 0" in rcoutput,
            "560 Hz must be restricted to IOMCU outputs 1-4")
    require("if (group_freq > 400 && !allow_kst_560hz" in rcoutput,
            "Non-KST high PWM rates must retain the 400 Hz clamp")
    require("group.rc_frequency == 560" in rcoutput and
            "period_us != 0" in rcoutput and
            "MIN(960U, MAX(560U, period_us))" in rcoutput,
            "KST pulses must be limited to 560-960 us while preserving zero")
    require("const uint8_t ch_masks[3] = { 0x03,0x0C,0xF0 }" in iomcu_header,
            "IOMCU timer-group masks changed; verify MAIN6 remains at default 50 Hz")
    require("uint32_t mask = (1U << AP_MOTORS_MOT_4) | _swashplate.get_output_mask()" in heli_motors,
            "Traditional heli fast-rate mask no longer covers the four intended motor outputs")
    require("#if FRAME_CONFIG == HELI_FRAME" in parameters and
            "// @Range: 50 560" in parameters and
            "// @Range: 50 490" in parameters,
            "Only heli metadata may advertise the 560 Hz setting")
    require(srv_parameters.count("// @Range: 500 2200") >= 3,
            "Servo MIN/TRIM/MAX metadata must allow manufacturer-validated narrow pulses")
    require('AP_GROUPINFO("PWM_MODE", 9, AP_MotorsHeli_Swash, _swash_pwm_mode, 0)' in swash and
            "if (_swash_pwm_mode != 1)" in swash and
            "set_output_norm_trimmed(function, swash_in)" in swash,
            "Swash narrow-pulse mapping must be opt-in with legacy default retained")
    require(swash.index('AP_GROUPINFO("PWM_MODE", 9,') <
            swash.index('AP_GROUPINFO_FLAGS("H3_ENABLE", 4,'),
            "PWM_MODE must precede H3_ENABLE or the MAVLink parameter list can hide it")
    require("// @Param: SW_PWM_MODE" in heli_single and
            "// @Param: SW2_PWM_MODE" in heli_dual,
            "Swash PWM modes must have complete ground-station parameter descriptions")
    require("trim - minimum" in srv_output and "maximum - trim" in srv_output and
            "channel.get_reversed() ? -command : command" in srv_output and
            "channel.get_trim() < channel.get_output_max()" in srv_output,
            "Per-servo normalized mapping must honor endpoints, trim, and reversal")
    require("_swashplate.output_ranges_valid()" in heli_single and
            "_swashplate1.output_ranges_valid()" in heli_dual and
            "_swashplate2.output_ranges_valid()" in heli_dual,
            "Invalid configured swash ranges must fail existing arming checks")
    require('AP_SUBGROUPINFO(_swashplate, "SW_"' in heli_single and
            'AP_SUBGROUPINFO(_swashplate2, "SW2_"' in heli_dual,
            "Single and dual swash settings must retain the H_SW_ and H_SW2_ prefixes")
    require("case PAGE_PWM_INFO:" in iomcu_fw and
            "case PAGE_FAILSAFE_PWM:" in iomcu_fw,
            "IOMCU readback pages are missing")
    diagnostic_start = iomcu_fmu.find("static uint32_t last_kst_diagnostic_ms")
    diagnostic_end = iomcu_fmu.find("check_iomcu_reset();", diagnostic_start)
    require(diagnostic_start >= 0 and diagnostic_end > diagnostic_start,
            "Read-only IOMCU diagnostic block is missing")
    diagnostic = iomcu_fmu[diagnostic_start:diagnostic_end]
    require("read_registers(" in diagnostic and "write_register" not in diagnostic,
            "IOMCU diagnostics must remain read-only")
    require("failsafe_pwm[IOMCU_MAX_RC_CHANNELS]" in diagnostic and
            "read_registers(PAGE_FAILSAFE_PWM" in diagnostic and
            "failsafe_pwm[i] != 0" in diagnostic,
            "IOMCU failsafe diagnostic must observe zero without changing failsafe")
    require("#define RPM_MAX_INSTANCES 2" in rpm_config and
            'AP_SUBGROUPINFO(_params[0], "1_"' in rpm_source and
            'AP_SUBGROUPINFO(_params[1], "2_"' in rpm_source and
            "50:AUX1,51:AUX2,52:AUX3,53:AUX4,54:AUX5,55:AUX6" in rpm_params and
            "PWM5 and PWM6 will be available for" in board_readme,
            "RPM1 GPIO support or Pixhawk4 GPIO mapping changed; verify before migration")
    require("DROPOUT" not in rcoutput and "DROPOUT" not in iomcu_fw and
            "DROPOUT" not in iomcu_fmu,
            "Temporary FMU-dropout test code must not enter the release")

    print("PASS: Pixhawk4 timer groups, KST rate/pulse bounds, swash mapping/ranges,")
    print("      ESC rate metadata, read-only failsafe/IOMCU diagnostics, dropout exclusion")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)