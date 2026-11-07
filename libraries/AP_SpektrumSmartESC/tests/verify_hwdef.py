#!/usr/bin/env python3
# AP_FLAKE8_CLEAN
"""Generate both hwdefs and check preservation of adjacent output mappings."""
import argparse
import pathlib
import subprocess
import sys


def macro(text, name):
    lines = iter(text.splitlines())
    for line in lines:
        if line.startswith('#define ' + name + ' '):
            result = [line]
            while result[-1].endswith('\\'):
                result.append(next(lines))
            return '\n'.join(result)
    raise RuntimeError(f'Missing generated macro {name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board', choices=('Pixhawk4', 'Pixhawk6C'), default='Pixhawk6C')
    args = parser.parse_args()
    board_name = args.board
    variant_name = board_name + '-SpktSmart'
    root = pathlib.Path(__file__).resolve().parents[3]
    hwdef = root / 'libraries/AP_HAL_ChibiOS/hwdef'
    output = root / 'build/spektrum-smart-hwdef'
    generated = {}
    for board in (board_name, variant_name):
        directory = output / board
        directory.mkdir(parents=True, exist_ok=True)
        result = subprocess.run(
            [sys.executable, str(hwdef / 'scripts/chibios_hwdef.py'), '-D', str(directory),
             '--params', str(hwdef / board / 'defaults.parm'),
             str(hwdef / board / 'hwdef.dat')], cwd=root, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True)
        (directory / 'generation.log').write_text(result.stdout, encoding='utf-8')
        if result.returncode != 0:
            print(result.stdout)
            return result.returncode
        generated[board] = (directory / 'hwdef.h').read_text()
    baseline = generated[board_name]
    variant = generated[variant_name]
    timer_mapping = '{255, 1, 2, 3}' if board_name == 'Pixhawk6C' else '{3, 2, 1, 255}'
    reserved_pin = 'PAL_LINE(GPIOA,8U)' if board_name == 'Pixhawk6C' else 'PAL_LINE(GPIOE,14U)'
    first_group = 'HAL_PWM_GROUP3' if board_name == 'Pixhawk6C' else 'HAL_PWM_GROUP1'
    last_group = 'HAL_PWM_GROUP1' if board_name == 'Pixhawk6C' else 'HAL_PWM_GROUP3'
    last_group_preserved = (macro(baseline, last_group) == macro(variant, last_group))
    if board_name == 'Pixhawk6C':
        last_group_preserved = ('{255, 7, 255, 255}' in macro(variant, last_group) and
                                'PAL_LINE(GPIOA,1U)' in macro(variant, last_group) and
                                'PAL_LINE(GPIOA,0U)' not in macro(variant, last_group))
    pwm_count = 6 if board_name == 'Pixhawk6C' else 7
    checks = [
        ('AUX1 removed, AUX2-4 mapping retained',
         timer_mapping in macro(variant, first_group) and
         reserved_pin not in macro(variant, first_group)),
        ('AUX5-6 mapping retained', macro(baseline, 'HAL_PWM_GROUP2') == macro(variant, 'HAL_PWM_GROUP2')),
        ('AUX8 retained, AUX7 reserved' if board_name == 'Pixhawk6C' else 'AUX7-8 mapping retained',
         last_group_preserved),
        (f'{pwm_count} physical PWM outputs',
         macro(variant, 'HAL_PWM_COUNT') == f'#define HAL_PWM_COUNT {pwm_count}'),
        ('Board defaults preserved',
         (output / board_name / 'processed_defaults.parm').read_text() ==
         (output / variant_name / 'processed_defaults.parm').read_text()),
    ]
    if board_name == 'Pixhawk6C':
        checks.append(('AUX7 input at boot, UART4 reserved outside SERIALn, hardware TX forbidden',
                       'PA0 SPKTSMART_AUX7_RESERVED INPUT' in variant and
                       'HAL_UART4_CONFIG' not in variant and
                       '#define HAL_SPEKTRUM_SMART_ESC_UART4_RX 1' in variant and
                       '#define AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED 0' in variant and
                       '{  56,' not in variant))
    for name, passed in checks:
        print(f'{"PASS" if passed else "FAIL"}: {name}')
    return 0 if all(passed for _, passed in checks) else 1


if __name__ == '__main__':
    raise SystemExit(main())
