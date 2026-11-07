#!/usr/bin/env python3
# AP_FLAKE8_CLEAN
"""Generate an uninstalled UART4/AUX7 candidate to check resource ownership.

This does not alter any firmware target or enable a physical UART.
"""
import pathlib
import re
import subprocess
import sys

from verify_hwdef import macro


def main():
    root = pathlib.Path(__file__).resolve().parents[3]
    hwdef = root / 'libraries/AP_HAL_ChibiOS/hwdef'
    out = root / 'build/spektrum-smart-aux7-candidate'
    out.mkdir(parents=True, exist_ok=True)
    candidate = out / 'hwdef.dat'
    candidate.write_text(
        'include ../../libraries/AP_HAL_ChibiOS/hwdef/Pixhawk6C-SpktSmart/hwdef.dat\n'
        'undef PA0\n'
        'PA0 UART4_TX UART4 NODMA OPENDRAIN PULLUP\n'
        'undef SERIAL_ORDER\n'
        'SERIAL_ORDER OTG1 UART7 UART5 USART1 UART8 USART2 USART3 OTG2 UART4\n',
        encoding='utf-8',
    )
    result = subprocess.run(
        [sys.executable, str(hwdef / 'scripts/chibios_hwdef.py'), '-D', str(out),
         '--params', str(hwdef / 'Pixhawk6C/defaults.parm'), str(candidate)],
        cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (out / 'generation.log').write_text(result.stdout, encoding='utf-8')
    if result.returncode:
        print(result.stdout)
        return result.returncode
    generated = (out / 'hwdef.h').read_text()
    tim5 = macro(generated, 'HAL_PWM_GROUP1')
    checks = [
        ('AUX7 is PA0 AF8 UART4_TX with open-drain and pull-up',
         'PA0 UART4_TX UART4 AF8' in generated and
         'PIN_OTYPE_OPENDRAIN(0U)' in macro(generated, 'VAL_GPIOA_OTYPER') and
         'PIN_PUPDR_PULLUP(0U)' in macro(generated, 'VAL_GPIOA_PUPDR')),
        ('AUX7 removed from TIM5, AUX8 keeps PWM8', '{255, 7, 255, 255}' in tim5),
        ('AUX8 still uses PA1', 'PAL_LINE(GPIOA,1U)' in tim5),
        ('UART4 TX has no DMA allocation', 'STM32_UART_UART4_TX_DMA_STREAM' not in generated),
        ('UART4 RX pin is not allocated', 'PA1 UART4_RX' not in generated),
        ('Existing serial port order retained with UART4 appended',
         re.findall(r'HAL_(?:OTG|UART|USART)\d+_CONFIG', macro(generated, 'HAL_SERIAL_DEVICE_LIST')) ==
         ['HAL_OTG1_CONFIG', 'HAL_UART7_CONFIG', 'HAL_UART5_CONFIG', 'HAL_USART1_CONFIG',
          'HAL_UART8_CONFIG', 'HAL_USART2_CONFIG', 'HAL_USART3_CONFIG', 'HAL_OTG2_CONFIG',
          'HAL_UART4_CONFIG', 'HAL_USART6_CONFIG']),
    ]
    for label, passed in checks:
        print(f'{"PASS" if passed else "FAIL"}: {label}')
    return 0 if all(passed for _, passed in checks) else 1


if __name__ == '__main__':
    raise SystemExit(main())
