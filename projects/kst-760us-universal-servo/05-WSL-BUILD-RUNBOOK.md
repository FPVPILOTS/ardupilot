# WSL development and build runbook

## Working location

Windows clone:
`D:\Users\Sietse\DEVELOPMENT EXPRESSELRS\ArduPilot-Project\ArduPilot-KST-Servo`

WSL Ubuntu view:
`/mnt/d/Users/Sietse/DEVELOPMENT EXPRESSELRS/ArduPilot-Project/ArduPilot-KST-Servo`

These are the **same files** viewed from different operating systems. In PowerShell, `cd /mnt/d/...` is invalid; enter `wsl` first.

Branch: `feature/kst-760us-universal-servo-support`.

## Start and verify (WSL shell, not PowerShell)

```bash
cd "/mnt/d/Users/Sietse/DEVELOPMENT EXPRESSELRS/ArduPilot-Project/ArduPilot-KST-Servo"
git status --short
git branch --show-current
git log -1 --format='%H %s'
git fetch origin
git pull --ff-only
git submodule update --init --recursive
python3 --version
arm-none-eabi-gcc --version
./waf --help
```

**Never run `git reset --hard` on local uncommitted work.** If `git pull --ff-only` fails or `git status` is not clean, stop and inspect changes first.

Building on a Windows NTFS-mounted folder (`/mnt/d`) can be slower and may expose permission, symlink or filesystem constraints; if the build encounters these, create a second Linux-native clone under `~/ardupilot-kst-servo` while keeping the D: clone as the requested editor location.

## Prerequisites (only if unavailable)

Official setup: https://ardupilot.org/dev/docs/building-setup-linux.html

```bash
Tools/environment_install/install-prereqs-ubuntu.sh -y
. ~/.profile
```

Confirm the Ubuntu release is supported by the prerequisite script. Record versions of gcc, arm-none-eabi-gcc, python3, waf, git, and submodules.

## First test builds

```bash
./waf configure --board sitl
./waf copter
```

The hardware board ID/name needs confirmation from `./waf list_boards` and the **actual Pixhawk4 firmware target**, followed by (example only, do not assume this target without checking):

```bash
./waf list_boards | grep -i pixhawk
# ./waf configure --board <verified-target>
# ./waf copter
```

Reconfigure before switching target; save complete stdout/stderr logs including commit SHA, warnings and memory use. Build Plane/Rover separately after SITL/heli compilation.

## Simulator

Consult `Tools/autotest/sim_vehicle.py --help` and available helicopter SITL models in this exact checkout. Do not invent an unsupported `-f` frame argument. Before any SITL commands, capture a baseline parameter dump from a clean legacy configuration and use independent SITL directories for mode 0 and mode 1.

## Deployment control

Do not flash hardware before source compilation, automated tests, parameter compatibility review, and waveform validation plan are complete. Save known-good firmware and a full .param backup.

## Communication format

Every build report must include: exact branch SHA, target board, command, toolchain versions, pass/fail, warning count, firmware binary path, and tests run.
