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
    passive_telem_executable = args.out.resolve() / 'test_spektrum_passive_telem.exe'
    offline_poll_executable = args.out.resolve() / 'test_spektrum_telem_poll_sim.exe'
    poll_dryrun_executable = args.out.resolve() / 'test_spektrum_poll_dryrun.exe'
    poll_lab_executable = args.out.resolve() / 'test_spektrum_poll_lab.exe'
    bench6_executable = args.out.resolve() / 'test_spektrum_bench6.exe'
    rx_witness_executable = args.out.resolve() / 'test_spektrum_post_poll_diag.exe'
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
             str(library / 'AP_SpektrumSmartESC.cpp'),
             str(library / 'AP_SpektrumSmartESC_Reader.cpp'),
             str(library / 'tests/test_spektrum_lab_lease.cpp'), '-o', str(lease_executable)],
            [str(lease_executable)],
            [args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-DSPKTSMART_STANDALONE_TEST',
             str(library / 'AP_SpektrumSmartESC.cpp'),
             str(library / 'tests/test_spektrum_passive_telem.cpp'),
             '-o', str(passive_telem_executable)],
            [str(passive_telem_executable)],
            # Offline-only 0xCD ReplyID experiment. No HAL, UART or firmware
            # integration is linked, and this executable never runs on a board.
            [args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-DSPKTSMART_STANDALONE_TEST',
             str(library / 'AP_SpektrumSmartESC.cpp'),
             str(library / 'tests/test_spektrum_telem_poll_sim.cpp'),
             '-o', str(offline_poll_executable)],
            [str(offline_poll_executable)],
            [args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-DSPKTSMART_STANDALONE_TEST',
             str(library / 'AP_SpektrumSmartESC.cpp'),
             str(library / 'AP_SpektrumSmartESC_Reader.cpp'),
             str(library / 'tests/test_spektrum_poll_dryrun.cpp'),
             '-o', str(poll_dryrun_executable)],
            [str(poll_dryrun_executable)],
            # Strict packet and native one-shot lease; physical transmission
            # requires the separately compiled PollLab board and operator consent.
            [args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-DSPKTSMART_STANDALONE_TEST',
             str(library / 'AP_SpektrumSmartESC.cpp'),
             str(library / 'AP_SpektrumSmartESC_Reader.cpp'),
             str(library / 'tests/test_spektrum_poll_lab.cpp'),
             '-o', str(poll_lab_executable)],
            [str(poll_lab_executable)],
            # Host-only 6% limit and reverse interlock contract. No HAL, UART,
            # on-device output or automatic motor command is compiled here.
            [args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror',
             str(library / 'tests/test_spektrum_bench6.cpp'),
             '-o', str(bench6_executable)],
            [str(bench6_executable)],
            # Post-poll RX parsing is observation-only and has no native TX.
            [args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror',
             '-DSPKTSMART_STANDALONE_TEST',
             str(library / 'AP_SpektrumSmartESC.cpp'),
             str(library / 'tests/test_spektrum_post_poll_diag.cpp'),
             '-o', str(rx_witness_executable)],
            [str(rx_witness_executable)],
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
