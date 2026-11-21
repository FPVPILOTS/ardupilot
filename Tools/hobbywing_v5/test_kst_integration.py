"""Source-level regression guards for the optional 760us / 560Hz KST port.

These inspect invariants in the actual stable integration code. They do NOT
replace compiling the two targets or measuring IOMCU PWM waveforms.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class KSTPortSourceGuards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.swash = (ROOT / "libraries/AP_Motors/AP_MotorsHeli_Swash.cpp").read_text()
        cls.header = (ROOT / "libraries/AP_Motors/AP_MotorsHeli_Swash.h").read_text()
        cls.servo = (ROOT / "libraries/SRV_Channel/SRV_Channel.cpp").read_text()
        cls.rcout = (ROOT / "libraries/AP_HAL_ChibiOS/RCOutput.cpp").read_text()

    def test_default_mode_is_legacy(self):
        self.assertIn('AP_GROUPINFO("PWM_MODE", 9, AP_MotorsHeli_Swash, _swash_pwm_mode, 0)', self.swash)
        self.assertIn('SRV_Channels::set_output_min_max(function, 1000, 2000)', self.swash)
        self.assertIn('SRV_Channels::set_output_pwm_trimmed(function, pwm)', self.swash)

    def test_opt_in_respects_servo_trim_and_endpoints(self):
        self.assertIn('if (_swash_pwm_mode.get() == 1)', self.swash)
        self.assertIn('SRV_Channels::set_angle(function, 500)', self.swash)
        self.assertIn('SRV_Channels::set_output_norm(function, constrain_float(swash_in, -1.0f, 1.0f))', self.swash)
        self.assertIn('AP_Int8  _swash_pwm_mode', self.header)
        self.assertEqual(self.servo.count('// @Range: 500 2200'), 3)

    def test_fast_pwm_restricted_to_iomcu_first_group(self):
        self.assertIn('freq_hz == 560 && group.ch_mask != 0', self.rcout)
        self.assertIn('(group.ch_mask & ~0x0FU) == 0', self.rcout)
        self.assertIn('group_freq > 400 && !allow_kst_560hz', self.rcout)
        self.assertIn('group.rc_frequency == 560', self.rcout)
        self.assertIn('period_us = MIN(960U, MAX(560U, period_us))', self.rcout)
        self.assertIn('period_us != 0', self.rcout)


if __name__ == "__main__":
    unittest.main()
