#!/usr/bin/env python3
"""Ensure every frozen active legacy rule is included in migration parity."""

import json
from pathlib import Path

import compare


ROOT = Path(__file__).resolve().parent
rules = json.loads((ROOT / "rules.json").read_text(encoding="utf-8"))
active = {entry["id"] for entry in rules if entry["status"] == "active"}
disabled = {entry["id"] for entry in rules if entry["status"] == "disabled"}
missing = active - compare.SUPPORTED
unexpected = compare.SUPPORTED - active
activated = disabled & compare.SUPPORTED

if missing or unexpected or activated:
    raise SystemExit(f"rule inventory mismatch: missing={sorted(missing)}, "
                     f"unexpected={sorted(unexpected)}, "
                     f"disabled={sorted(activated)}")

print(f"Rule inventory: {len(active)}/{len(active)} active legacy "
      "rules migrated; disabled rules remain disabled")
