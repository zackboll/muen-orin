import unittest

from contrib.source_manifest import derive_counts, parse_records


class SourceManifestTest(unittest.TestCase):
    def test_digest_classifications_and_multiple_scms(self):
        package = "target/example"
        records = parse_records(
            [
                f"url|{package}|sha1|https://example/one|one|a|<unset>|<unset>\n",
                f"url|{package}|sha256|https://example/two|two|<unset>|b|<unset>\n",
                f"url|{package}|sha512|https://example/three|three|<unset>|<unset>|c\n",
                f"url|{package}|multi|https://example/four|four|a|b|c\n",
                f"url|{package}|none|https://example/five|five|<unset>|<unset>|<unset>\n",
            ]
        )

        self.assertEqual(len(records), 5)
        self.assertEqual(
            [record["record_number"] for record in records], list(range(1, 6))
        )
        self.assertEqual(
            [record["digest_classification"] for record in records],
            [
                "sha1_only",
                "sha256_or_sha512",
                "sha256_or_sha512",
                "sha256_or_sha512",
                "no_supported_digest",
            ],
        )
        counts = derive_counts(records)
        self.assertEqual(counts["url_sha1_only"], 1)
        self.assertEqual(counts["url_sha256_or_sha512"], 3)
        self.assertEqual(counts["url_with_configured_sha1"], 2)
        self.assertEqual(counts["url_with_configured_sha256"], 2)
        self.assertEqual(counts["url_with_configured_sha512"], 2)
        self.assertEqual(counts["url_no_supported_digest"], 1)
        self.assertEqual(counts["url_unknown_digest_evidence"], 0)
        self.assertEqual(counts["url_with_multiple_configured_digests"], 1)

    def test_git_fields_and_invalid_input(self):
        records = parse_records(
            ["git|target/git|src|https://example/repo.git|main|abc|<unset>\n"]
        )
        self.assertEqual(records[0]["commit"], "abc")
        self.assertIsNone(records[0]["tag"])
        with self.assertRaises(ValueError):
            parse_records(["url|too|few\n"])


if __name__ == "__main__":
    unittest.main()
