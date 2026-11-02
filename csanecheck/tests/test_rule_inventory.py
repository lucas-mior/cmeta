#!/usr/bin/env python3
"""Check the guide and checker enforcement inventory."""

import csv
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
INVENTORY = ROOT / "csanecheck/rule_inventory.tsv"
CLASSIFICATIONS = {"implemented", "csanecheck_candidate", "compiler", "manual"}
RULE_ID = re.compile(r'"([A-Z][A-Z0-9-]+)"')


def load_inventory():
    with INVENTORY.open(encoding="utf-8") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def checker_rule_ids():
    result = set()
    for path in (ROOT / "csanecheck").glob("*.c"):
        text = path.read_text(encoding="utf-8")
        result.update(RULE_ID.findall(text))
    return result


class RuleInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = load_inventory()

    def test_schema_ids_ranges_and_rule_ids_are_valid(self):
        ids = [row["inventory_id"] for row in self.rows]
        self.assertEqual(len(ids), len(set(ids)))
        known_rules = checker_rule_ids()
        for row in self.rows:
            with self.subTest(inventory_id=row["inventory_id"]):
                self.assertIn(row["classification"], CLASSIFICATIONS)
                source = ROOT / row["source"]
                self.assertTrue(source.is_file(), row["source"])
                line_count = len(source.read_text(encoding="utf-8").splitlines())
                start = int(row["line_start"])
                end = int(row["line_end"])
                self.assertGreaterEqual(start, 1)
                self.assertGreaterEqual(end, start)
                self.assertLessEqual(end, line_count)
                self.assertTrue(row["requirement"].strip())
                rule_ids = row["csane_rule_ids"]
                if rule_ids != "-":
                    for rule_id in rule_ids.split(";"):
                        self.assertIn(rule_id, known_rules)

    def test_inventory_has_all_four_enforcement_classes(self):
        classes = {row["classification"] for row in self.rows}
        self.assertEqual(classes, CLASSIFICATIONS)


if __name__ == "__main__":
    unittest.main()
