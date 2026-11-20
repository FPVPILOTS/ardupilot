#!/usr/bin/env python3
"""Read-only ArduPlane Viper 90mm configuration audit.

Usage: python3 Tools/hobbywing_v5/audit_viper_plane.py "viper test(2).param"
Only assesses supplied parameter values. Never writes parameters or arms a vehicle.
"""
import argparse
import re
from pathlib import Path

def parse_params(path):
    params = {}
    for number, line in enumerate(Path(path).read_text(encoding="utf-8-sig").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith(("#", "//")):
            continue
        tokens = re.split(r"[,\s]+", line, maxsplit=1)
        if len(tokens) != 2:
            continue
        name = tokens[0].upper()
        value = tokens[1].split("#", 1)[0].strip()
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name):
            continue
        try:
            params[name] = float(value)
        except ValueError:
            print(f"INFO line {number}: ignored nonnumeric {name}")
    return params

def audit(p):
    results = []
    def check(name, expected=None, allowed=None, severity="BLOCK", note=""):
        value = p.get(name)
        if value is None:
            results.append(("REVIEW", name, "-", "parameter absent from export"))
        elif expected is not None and value != expected:
            results.append((severity, name, str(value), f"expected {expected}. {note}".strip()))
        elif allowed is not None and value not in allowed:
            results.append((severity, name, str(value), f"expected one of {allowed}. {note}".strip()))
        else:
            results.append(("OK", name, f"{value:g}", note or "parameter value matches planned configuration"))
    check("SERVO3_FUNCTION", 70, note="Viper ESC throttle")
    check("SERVO11_FUNCTION", 150, note="RCIN11Scaled reverse, validated mapping")
    check("SERVO_BLH_HWV5", 1, severity="REVIEW", note="receive-only Hobbywing telemetry")
    check("SERVO_BLH_HWOUT", 3, severity="REVIEW", note="ESC telemetry mapped to output 3")
    check("SERVO_BLH_HWRPM", 1, severity="REVIEW", note="mechanical RPM enabled")
    check("SERVO_BLH_POLES", 4, severity="REVIEW", note="verify exact motor pole count")
    check("ARMING_CHECK", allowed=(-1, 1), note="full checks enabled; never disable to bypass faults")
    check("BATT_FS_LOW_ACT", 1, note="low battery triggers RTL")
    check("BATT_FS_CRT_ACT", allowed=(1, 7), note="critical action must be validated in vehicle/SITL; 7 requires AUTOLAND setup")
    check("BATT_FS_VOLTSRC", allowed=(0, 1), severity="REVIEW", note="0 raw, 1 sag-compensated")
    check("FS_LONG_ACTN", allowed=(0, 1), severity="REVIEW", note="verify RTL effect in selected flight modes")
    check("SERVO_BLH_HWGAIN", 1, severity="REVIEW", note="keep ESC-current gain uncalibrated")
    check("SERVO_BLH_HWCOFF", 0, severity="REVIEW", note="do not fabricate ESC current")
    for name in ("BATT_MONITOR","BATT_CAPACITY","BATT_LOW_VOLT","BATT_CRT_VOLT",
                 "BATT_LOW_MAH","BATT_CRT_MAH","BATT_LOW_TIMER","BATT_ARM_VOLT",
                 "FS_SHORT_ACTN","FS_LONG_TIMEOUT","RC_OPTIONS","SERVO_RC_FS_MSK",
                 "RTL_ALTITUDE","RTL_RADIUS","RTL_AUTOLAND","SERVO_RATE","RC_SPEED"):
        if name in p:
            results.append(("REVIEW",name,f"{p[name]:g}","inspect against aircraft test evidence"))
        else:
            results.append(("REVIEW",name,"-","not available in supplied export"))
    # Explicit checks for missing or inconsistent voltage/capacity triggers.
    for name in ("BATT_CAPACITY","BATT_LOW_VOLT","BATT_CRT_VOLT","BATT_LOW_MAH","BATT_CRT_MAH"):
        if p.get(name,0) <= 0:
            results.append(("REVIEW",name,str(p.get(name,"-")),"verify intentional disabled threshold and usable battery energy reserve"))
    if p.get("BATT_LOW_VOLT",0)>0 and p.get("BATT_CRT_VOLT",0)>0 and p["BATT_CRT_VOLT"]>=p["BATT_LOW_VOLT"]:
        results.append(("BLOCK","BATT_CRT_VOLT",f'{p["BATT_CRT_VOLT"]:g}',"critical threshold should be below low threshold"))
    if p.get("BATT_LOW_MAH",0)>0 and p.get("BATT_CRT_MAH",0)>0 and p["BATT_CRT_MAH"]>=p["BATT_LOW_MAH"]:
        results.append(("BLOCK","BATT_CRT_MAH",f'{p["BATT_CRT_MAH"]:g}',"critical remaining mAh should be below low remaining mAh"))
    if p.get("BATT_CAPACITY",0)>0:
        for name in ("BATT_LOW_MAH","BATT_CRT_MAH"):
            if p.get(name,0)>p["BATT_CAPACITY"]:
                results.append(("BLOCK",name,f'{p[name]:g}',"remaining threshold exceeds configured total capacity"))
    return results

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("parameters", help="Mission Planner .param export")
    parser.add_argument("--strict",action="store_true",help="return nonzero on BLOCK")
    args=parser.parse_args()
    p=parse_params(args.parameters)
    print(f"Viper 90mm ArduPlane audit: {len(p)} parameters (READ ONLY)")
    result=audit(p)
    for status,name,value,reason in result:
        print(f"{status:6} {name:23} {value:>8}  {reason}")
    count={status:sum(item[0]==status for item in result) for status in ("OK","REVIEW","BLOCK")}
    print(f"SUMMARY: {count['OK']} OK, {count['REVIEW']} REVIEW, {count['BLOCK']} BLOCK")
    print("AUDIT NOT FLIGHT CLEARANCE: verify reverse under loss, PWM rate, motor-safe tests, actual RTL and battery reserve.")
    if args.strict and count["BLOCK"]:
        raise SystemExit(2)

if __name__=="__main__":
    main()
