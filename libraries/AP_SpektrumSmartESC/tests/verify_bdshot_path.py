#!/usr/bin/env python3
# AP_FLAKE8_CLEAN
"""Check the official bdshot resource declaration; not a board electrical test."""
import pathlib
import re
import subprocess
import sys

from verify_hwdef import macro


def main():
    root = pathlib.Path(__file__).resolve().parents[3]
    hwdef = root / 'libraries/AP_HAL_ChibiOS/hwdef'
    output = root / 'build/spektrum-smart-bdshot-audit'
    output.mkdir(parents=True, exist_ok=True)
    command = [sys.executable, str(hwdef / 'scripts/chibios_hwdef.py'), '-D', str(output),
               '--params', str(hwdef / 'Pixhawk6C/defaults.parm'),
               str(hwdef / 'Pixhawk6C-bdshot/hwdef.dat')]
    result = subprocess.run(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (output / 'generation.log').write_text(result.stdout, encoding='utf-8')
    if result.returncode:
        print(result.stdout)
        return result.returncode
    generated = (output / 'hwdef.h').read_text()
    # DMA ordering differs between stock and bdshot definitions. Find TIM5
    # by driver identity rather than assuming the same generated group index.
    group = next(macro(generated, name) for name in re.findall(r'#define (HAL_PWM_GROUP\d+)\b', generated)
                 if 'PWMD5' in macro(generated, name))
    checks = [
        ('Official AUX7 declaration is PA0 TIM5_CH1 PWM7 BIDIR',
         'PA0  TIM5_CH1 TIM5 PWM(7) GPIO(56) BIDIR' in (hwdef / 'Pixhawk6C-bdshot/hwdef.dat').read_text()),
        ('Generated bdshot support enabled', '#define HAL_WITH_BIDIR_DSHOT' in generated),
        ('AUX7 and AUX8 share TIM5, original numbering retained',
         '{6, 7, 255, 255}' in group and 'PWMD5' in group),
        ('Input capture TIM5_CH1 DMA is allocated', 'STM32_TIM_TIM5_CH1_DMA_STREAM' in generated),
        ('Timer update TIM5 DMA is allocated', 'STM32_TIM_TIM5_UP_DMA_STREAM' in generated),
        ('Bdshot uses timer AF2, not UART4 AF8',
         'PA0 TIM5_CH1 TIM5 AF2 PWM7' in generated and 'HAL_UART4_CONFIG' not in generated),
    ]
    for label, passed in checks:
        print(f'{"PASS" if passed else "FAIL"}: {label}')
    return 0 if all(passed for _, passed in checks) else 1


if __name__ == '__main__':
    raise SystemExit(main())
