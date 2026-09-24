#!/usr/bin/env python3
"""Generate the Task 001 SCM manifest from deterministic Bob query output."""

import argparse
import json
from collections import Counter
from pathlib import Path


UNSET = "<unset>"


def optional(value):
    return None if value == UNSET else value


def digest_classification(digests):
    """Classify configured URL digests without treating absent SHA-256 alone."""
    present = {name for name, value in digests.items() if value is not None}
    if "sha256" in present or "sha512" in present:
        return "sha256_or_sha512"
    if present == {"sha1"}:
        return "sha1_only"
    if not present:
        return "no_supported_digest"
    return "unknown"


def parse_records(lines):
    """Parse Bob records in order; intentionally do not deduplicate them."""
    records = []
    for record_number, raw in enumerate(lines, 1):
        line = raw.rstrip("\n")
        fields = line.split("|")
        if fields[0] == "git" and len(fields) == 7:
            _, package, directory, url, branch, commit, tag = fields
            records.append(
                {
                    "record_number": record_number,
                    "scm": "git",
                    "package": package,
                    "directory": directory,
                    "url": url,
                    "branch": optional(branch),
                    "commit": optional(commit),
                    "tag": optional(tag),
                }
            )
        elif fields[0] == "url" and len(fields) == 8:
            _, package, directory, url, filename, sha1, sha256, sha512 = fields
            digests = {
                "sha1": optional(sha1),
                "sha256": optional(sha256),
                "sha512": optional(sha512),
            }
            records.append(
                {
                    "record_number": record_number,
                    "scm": "url",
                    "package": package,
                    "directory": directory,
                    "url": url,
                    "filename": filename,
                    "digests": digests,
                    "digest_classification": digest_classification(digests),
                }
            )
        else:
            raise ValueError(f"invalid SCM record {record_number}: {line!r}")
    return records


def derive_counts(records):
    scm = Counter(record["scm"] for record in records)
    digest = Counter(
        record["digest_classification"]
        for record in records
        if record["scm"] == "url"
    )
    return {
        "total": len(records),
        "git": scm["git"],
        "git_with_configured_commit": sum(
            record["commit"] is not None
            for record in records
            if record["scm"] == "git"
        ),
        "git_without_configured_commit": sum(
            record["commit"] is None
            for record in records
            if record["scm"] == "git"
        ),
        "url": scm["url"],
        "url_with_configured_sha1": sum(
            record["digests"]["sha1"] is not None
            for record in records
            if record["scm"] == "url"
        ),
        "url_with_configured_sha256": sum(
            record["digests"]["sha256"] is not None
            for record in records
            if record["scm"] == "url"
        ),
        "url_with_configured_sha512": sum(
            record["digests"]["sha512"] is not None
            for record in records
            if record["scm"] == "url"
        ),
        "url_sha256_or_sha512": digest["sha256_or_sha512"],
        "url_sha1_only": digest["sha1_only"],
        "url_no_supported_digest": digest["no_supported_digest"],
        "url_unknown_digest_evidence": digest["unknown"],
        "url_with_multiple_configured_digests": sum(
            sum(value is not None for value in record["digests"].values()) > 1
            for record in records
            if record["scm"] == "url"
        ),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("query_output", type=Path)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    with args.query_output.open(encoding="utf-8") as source:
        records = parse_records(source)
    with args.metadata.open(encoding="utf-8") as source:
        manifest = json.load(source)

    manifest["counts"] = derive_counts(records)
    manifest["scm_records"] = records
    with args.output.open("w", encoding="utf-8") as output:
        json.dump(manifest, output, indent=2)
        output.write("\n")


if __name__ == "__main__":
    main()
