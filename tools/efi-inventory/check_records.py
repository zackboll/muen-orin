#!/usr/bin/env python3
"""Decode every captured record with a strict JSON parser and check values.

tests.c already checks exact byte fragments; this independently checks that
each record is valid, bounded, printable-ASCII JSON and that decoded values
match for the key regression cases.
"""
import json
import pathlib
import sys

RECORD_CAPACITY = 4096
STATUSES = {"observed", "unavailable", "unsupported", "malformed", "not_probed", "truncated"}
OBJECTS = ("firmware_vendor", "loaded_image", "device_path", "boot_services_map",
           "firmware_dt", "SecureBoot", "SetupMode", "CurrentEL")

EXPECT = {
    "t_map_release_failure": {("boot_services_map", "release_status"): "0x8000000000000002"},
    "t_map_release_failure_stops_retry": {("boot_services_map", "release_status"): "0x8000000000000002",
                                          ("boot_services_map", "attempts"): 1},
    "t_var_secure1_setup0": {("SecureBoot", "value"): 1, ("SetupMode", "value"): 0},
    "t_var_secure0_setup1": {("SecureBoot", "value"): 0, ("SetupMode", "value"): 1},
    "t_var_malformed_size": {("SecureBoot", "status"): "malformed", ("SetupMode", "value"): 1},
    "t_var_invalid_value": {("SetupMode", "status"): "malformed", ("SetupMode", "detail"): "value_range"},
    "t_var_not_found": {("SecureBoot", "status"): "unavailable", ("SecureBoot", "value"): None},
    "t_var_buffer_too_small": {("SetupMode", "efi_status"): "0x8000000000000005"},
    "t_map_size_exceeds_buffer": {("boot_services_map", "status"): "malformed",
                                  ("boot_services_map", "detail"): "size_exceeds_buffer",
                                  ("boot_services_map", "descriptors"): []},
    "t_map_retry_exhaustion": {("boot_services_map", "detail"): "retry_limit",
                               ("boot_services_map", "attempts"): 5},
    "t_report_dt_search_limit": {("firmware_dt", "status"): "not_probed",
                                 ("firmware_dt", "tables_searched"): 256},
    "t_report_dt_model_truncated": {("firmware_dt", "status"): "observed",
                                    ("firmware_dt", "model", "status"): "truncated",
                                    ("firmware_dt", "model", "length"): 100},
    "t_report_dt_model": {("firmware_dt", "model", "value"): 'NVIDIA "test" board'},
    "t_report_dt_model_escape": {("firmware_dt", "model", "value"): "tab\there\\back"},
    "t_vendor_non_ascii": {("firmware_vendor", "value"): "N\u00e9\u4e2d\ufffd"},
    "t_vendor_escapes": {("firmware_vendor", "value"): 'a"\\\n\x1f\x7fz'},
    "t_vendor_truncated": {("firmware_vendor", "status"): "truncated",
                           ("firmware_vendor", "value"): "W" * 64},
}


def lookup(record, path):
    for part in path:
        record = record[part]
    return record


def main():
    directory = pathlib.Path(sys.argv[1])
    small = len(sys.argv) > 2 and sys.argv[2] == "--small"
    files = sorted(directory.glob("*.json"))
    failures = 0
    for path in files:
        raw = path.read_bytes()
        name = path.stem
        try:
            if not raw:
                # Cases that emit nothing (no ConOut / first output fails).
                assert name in ("t_output_missing", "t_output_function_missing",
                                "t_output_first_call_fails", "t_output_and_cleanup_failure",
                                "t_no_system_table"), "empty record"
                continue
            assert len(raw) < RECORD_CAPACITY, "record exceeds bound"
            assert all(0x20 <= b < 0x7F for b in raw), "non-printable-ASCII byte"
            text = raw.decode("ascii")
            if name in ("t_output_fails_mid_record", "t_output_fails_final_newline"):
                continue  # partial output by design; the return status is checked in C
            record = json.loads(text, parse_constant=lambda c: (_ for _ in ()).throw(ValueError(c)))
            if small:
                assert record == {"schema": 2, "record_truncated": True}
                continue
            assert record["schema"] == 2 and record["record_truncated"] is False
            for key in OBJECTS:
                assert record[key]["status"] in STATUSES, key
            for key_path, value in EXPECT.get(name, {}).items():
                actual = lookup(record, key_path)
                assert actual == value, "%s: %r != %r" % ("/".join(key_path), actual, value)
        except (AssertionError, KeyError, ValueError) as error:
            failures += 1
            print("FAIL %s: %s" % (name, error))
    missing = [] if small else sorted(set(EXPECT) - {p.stem for p in files})
    for name in missing:
        failures += 1
        print("FAIL %s: record missing" % name)
    print("%d records decoded, %d failed" % (len(files), failures))
    return 1 if failures or not files else 0


if __name__ == "__main__":
    sys.exit(main())
