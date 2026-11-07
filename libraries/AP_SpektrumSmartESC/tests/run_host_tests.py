#!/usr/bin/env python3
# AP_FLAKE8_CLEAN
"""Compile and run the actual C++ parser without HAL or GTest dependencies."""
import argparse
import pathlib
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--arm-cxx', default=None)
    parser.add_argument('--out', type=pathlib.Path, default=pathlib.Path('build/spektrum-smart-host'))
    args = parser.parse_args()
    library = pathlib.Path(__file__).resolve().parents[1]
    args.out.mkdir(parents=True, exist_ok=True)
    executable = args.out.resolve() / 'test_spektrum_smart.exe'
    recovery_executable = args.out.resolve() / 'test_spektrum_uart_recovery.exe'
    autoconnect_executable = args.out.resolve() / 'test_spektrum_autoconnect.exe'
    autosession_executable = args.out.resolve() / 'test_spektrum_autosession.exe'
    lease_executable = args.out.resolve() / 'test_spektrum_lab_lease.exe'
    sources = [str(library / 'AP_SpektrumSmartESC.cpp'), str(library / 'tests/test_spektrum_smart.cpp'),
               str(library / 'AP_SpektrumSmartESC_Handshake.cpp')]
    with (args.out / 'tests.log').open('w', encoding='utf-8') as log:
        commands = [
            [args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-DSPKTSMART_STANDALONE_TEST',
             *sources, '-o', str(executable)],
            [str(executable)],
            [args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-DSPKTSMART_STANDALONE_TEST',
             str(library / 'tests/test_spektrum_uart_recovery.cpp'), '-o', str(recovery_executable)],
            [str(recovery_executable)],
            [args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror',
             str(library / 'tests/test_spektrum_autoconnect.cpp'), '-o', str(autoconnect_executable)],
            [str(autoconnect_executable)],
            [args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-DSPKTSMART_STANDALONE_TEST',
             str(library / 'AP_SpektrumSmartESC.cpp'),
             str(library / 'AP_SpektrumSmartESC_Reader.cpp'),
             str(library / 'tests/test_spektrum_autosession.cpp'), '-o', str(autosession_executable)],
            [str(autosession_executable)],
            [args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-DSPKTSMART_STANDALONE_TEST',
             str(library / 'tests/test_spektrum_lab_lease.cpp'), '-o', str(lease_executable)],
            [str(lease_executable)],
        ]
        if args.arm_cxx:
            commands.append([
                args.arm_cxx, '-std=gnu++11', '-Os', '-Wall', '-Wextra', '-Werror', '-mcpu=cortex-m7',
                '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard', '-fno-exceptions', '-fno-rtti',
                '-c', sources[0], '-o', str(args.out.resolve() / 'protocol.o'),
            ])
        for command in commands:
            log.write(repr(command) + '\n')
            log.flush()
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            print(result.stdout, end='')
            log.write(result.stdout)
            log.write(f'exit={result.returncode}\n')
            if result.returncode != 0:
                return result.returncode
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
