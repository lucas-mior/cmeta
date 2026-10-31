#!/usr/bin/env python3
"""Check the guide and checker enforcement inventory."""

import csv
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
INVENTORY = ROOT / "csanecheck/rule_inventory.tsv"
GUIDES = (ROOT / "cbase/c-format.md", ROOT / "cbase/c-guidelines.md")
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


def section_ranges(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    headings = []
    for number, line in enumerate(lines, 1):
        if line.startswith("## ") or line.startswith("### "):
            headings.append((number, line.lstrip("# ")))
    ranges = []
    for index, (start, title) in enumerate(headings):
        end = headings[index + 1][0] - 1 if index + 1 < len(headings) else len(lines)
        ranges.append((start, end, title))
    return lines, ranges


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

    def test_every_guide_section_and_top_level_bullet_is_inventory_covered(self):
        by_source = {}
        for row in self.rows:
            by_source.setdefault(row["source"], []).append(
                (int(row["line_start"]), int(row["line_end"]), row["inventory_id"]))
        for guide in GUIDES:
            relative = guide.relative_to(ROOT).as_posix()
            entries = by_source.get(relative, [])
            lines, sections = section_ranges(guide)
            for start, end, title in sections:
                if title == "Note":
                    continue
                with self.subTest(source=relative, section=title):
                    self.assertTrue(
                        any(a <= end and b >= start for a, b, _ in entries),
                        f"no inventory entry for {relative}:{start} {title}")
            for number, line in enumerate(lines, 1):
                if not line.startswith("- "):
                    continue
                with self.subTest(source=relative, line=number):
                    self.assertTrue(
                        any(a <= number <= b for a, b, _ in entries),
                        f"uncovered top-level rule at {relative}:{number}: {line}")

    def test_inventory_has_all_four_enforcement_classes(self):
        classes = {row["classification"] for row in self.rows}
        self.assertEqual(classes, CLASSIFICATIONS)


if __name__ == "__main__":
    unittest.main()
