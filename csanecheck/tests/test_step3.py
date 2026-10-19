#!/usr/bin/env python3
"""Step 3 regressions: source selection across every input mechanism."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_compatibility import rule_counts, run_checker, write_file


BAD_SOURCE = "#ifdef FEATURE\n#endif\n"
CBASE_CALL = "void example(void) {\n    memcpy(0, 0, 0);\n}\n"


def files_checked(result):
    match = re.search(r"\bfiles=(\d+)\b", result.stdout)
    if match is None:
        raise AssertionError(result.stdout + result.stderr)
    return int(match.group(1))


class CsanecheckStep3Tests(unittest.TestCase):
    def assert_files(self, args, root, expected, overrides=None):
        result = run_checker([*args, "--stats"], root, overrides)
        self.assertIn(result.returncode, (0, 1), result.stderr)
        self.assertEqual(files_checked(result), expected,
                         result.stdout + result.stderr)
        return result

    def test_generated_header_ignored_with_all_input_modes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            generated = write_file(root, "src/produced.meta.h", BAD_SOURCE)
            normal = write_file(root, "src/normal.h", BAD_SOURCE)
            listing = write_file(root, "sources.list",
                                 f"{generated}\n{normal}\n")
            for args, count in ((["--root", root], 1),
                                (["--file", generated], 0),
                                (["--file-list", listing], 1),
                                ([generated], 0)):
                with self.subTest(args=args):
                    self.assert_files(args, root, count)

    def test_parent_directory_named_stc_is_not_excluded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "stc" / "project"
            write_file(root, "cbase/functions_never_use.txt", "")
            source = write_file(root, "src/regular.c", BAD_SOURCE)
            listing = write_file(root, "sources.list", f"{source}\n")
            self.assert_files(["--root", root], root, 1)
            self.assert_files(["--file", source], root, 1)
            self.assert_files(["--file-list", listing], root, 1)

    def test_stc_directory_excluded_by_every_input_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rejected = write_file(root, "src/stc/sub/bad.c", BAD_SOURCE)
            other = write_file(root, "tools/stc/deep/bad.h", BAD_SOURCE)
            accepted = write_file(root, "src/stc_file.c", BAD_SOURCE)
            listing = write_file(root, "sources.list",
                                 f"{rejected}\n{other}\n{accepted}\n")
            self.assert_files(["--root", root], root, 1)
            self.assert_files(["--file", rejected], root, 0)
            self.assert_files(["--file-list", listing], root, 1)
            self.assert_files([rejected, accepted], root, 1)

    def test_max_depth_zero_one_two_and_ryu_override(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "top.c", BAD_SOURCE)
            write_file(root, "src/first.c", BAD_SOURCE)
            write_file(root, "src/dir/second.c", BAD_SOURCE)
            write_file(root, "src/dir/deep/third.c", BAD_SOURCE)
            write_file(root, "cbase/ryu/inner/deep/ryu.c", BAD_SOURCE)
            for depth, count in ((0, 2), (1, 3), (2, 4), (3, 5)):
                with self.subTest(depth=depth):
                    self.assert_files(["--root", root, "--max-depth", depth],
                                      root, count)
            self.assert_files(["--root", root], root, 4,
                              {"TEST_MAXDEPTH": "2"})
            self.assert_files(["--root", root, "--max-depth", "0"],
                              root, 2, {"TEST_MAXDEPTH": "2"})

    def test_environment_filters_and_overrides(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "src/alpha.c", BAD_SOURCE)
            write_file(root, "src/beta.c", BAD_SOURCE)
            write_file(root, "src/always.h", BAD_SOURCE)
            self.assert_files(["--root", root], root, 2,
                              {"TEST_FILTER": "alpha"})
            self.assert_files(["--root", root, "--filter", "beta"], root, 2,
                              {"TEST_FILTER": "alpha"})
            self.assert_files(["--root", root], root, 2,
                              {"TEST_EXCLUDE_PATTERN": "alpha"})
            self.assert_files(["--root", root, "--exclude", "beta"],
                              root, 1, {"TEST_EXCLUDE_PATTERN": "alpha"})

    def test_filter_all_headers_and_direct_ryu_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "src/alpha.c", BAD_SOURCE)
            write_file(root, "src/ALPHA.C", BAD_SOURCE)
            write_file(root, "src/beta.h", BAD_SOURCE)
            write_file(root, "src/BETA.H", BAD_SOURCE)
            write_file(root, "cbase/ryu/d2s.c", BAD_SOURCE)
            write_file(root, "cbase/ryu/other/sub.c", BAD_SOURCE)
            self.assert_files(["--root", root, "--filter", "alpha"],
                              root, 4)
            self.assert_files(["--root", root, "--filter", "ryu"],
                              root, 4)
            self.assert_files(["--root", root, "--filter", "ryu.c"],
                              root, 4)

    def test_ryu_selection_in_standalone_cbase_project(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "functions_never_use.txt", "")
            write_file(root, "ryu/d2s.c", BAD_SOURCE)
            write_file(root, "ryu/deep/inner.c", BAD_SOURCE)
            write_file(root, "regular.c", BAD_SOURCE)
            self.assert_files(["--root", root, "--filter", "ryu"],
                              root, 1)
            self.assert_files(["--root", root, "--max-depth", "0"],
                              root, 3)

    def test_exclusion_anchor_is_project_relative_for_all_modes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "project"
            root.mkdir()
            source = write_file(root, "src/skip.c", BAD_SOURCE)
            other = write_file(root, "tools/keep.c", BAD_SOURCE)
            listing = write_file(root, "sources.list",
                                 f"{source}\n{other}\n")
            options = ["--exclude", "^src/"]
            self.assert_files(["--root", root, *options], root, 1)
            self.assert_files(["--file-list", listing, *options], root, 1)
            self.assert_files(["--file", source, *options], root, 0)
            self.assert_files([source, *options], root, 0)
            self.assert_files(["--root", "project", *options],
                              root.parent, 1)
            self.assert_files(["--file", "project/src/skip.c", *options],
                              root.parent, 0)
            self.assert_files(["--root", root], root, 1,
                              {"TEST_EXCLUDE_PATTERN": "^src/"})

    def test_cbase_symlink_policy_uses_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "cbase/functions_never_use.txt", "")
            write_file(root, "cbase/functions_allowed_cbase_only.txt",
                       "memcpy\n")
            inside = write_file(root, "cbase/inside.c", CBASE_CALL)
            outside = write_file(root, "src/outside.c", CBASE_CALL)
            from_cbase = root / "src/from_cbase.c"
            from_cbase.symlink_to(inside)
            from_src = root / "cbase/from_src.c"
            from_src.symlink_to(outside)
            for source, count in ((inside, 0), (outside, 1),
                                  (from_cbase, 0), (from_src, 1)):
                with self.subTest(source=source):
                    result = run_checker(["--file", source], root)
                    self.assertEqual(rule_counts(result)["API-CBASE-ONLY"],
                                     count, result.stdout + result.stderr)
            result = run_checker(["--root", root], root)
            self.assertEqual(rule_counts(result)["API-CBASE-ONLY"], 2,
                             result.stdout + result.stderr)

    def test_non_c_files_are_ignored_even_if_explicit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            other = write_file(root, "src/fixture.txt", BAD_SOURCE)
            accepted = write_file(root, "src/accepted.C", BAD_SOURCE)
            listing = write_file(root, "sources.list",
                                 f"{other}\n{accepted}\n")
            self.assert_files(["--file", other], root, 0)
            self.assert_files(["--file-list", listing], root, 1)
            self.assert_files(["--root", root], root, 1)

    def test_nested_non_stc_directories_are_not_excluded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "src/bin/inside.c", BAD_SOURCE)
            write_file(root, "src/.git/inside.h", BAD_SOURCE)
            write_file(root, "src/.test-tmp/inside.c", BAD_SOURCE)
            self.assert_files(["--root", root], root, 3)

    def test_discovery_matches_legacy_find_for_depth_and_filter(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "root.c", BAD_SOURCE)
            write_file(root, "src/one.c", BAD_SOURCE)
            write_file(root, "src/TWO.C", BAD_SOURCE)
            write_file(root, "src/header.H", BAD_SOURCE)
            write_file(root, "src/ignore.meta.h", BAD_SOURCE)
            write_file(root, "src/stc/skip.c", BAD_SOURCE)
            write_file(root, "tools/.test-tmp/nested.c", BAD_SOURCE)
            write_file(root, "cbase/ryu/d2s.c", BAD_SOURCE)
            write_file(root, "cbase/ryu/nested/other.c", BAD_SOURCE)
            (root / "src/alias.c").symlink_to(root / "src/one.c")
            (root / "tools/directory_link").symlink_to(
                root / "src", target_is_directory=True)
            directories = (".", "cbase", "src", "test", "tests",
                           "examples", "cyblocks", "tools")
            for depth, selected_filter in ((0, ""), (1, ""), (2, "one"),
                                           (3, "ryu")):
                with self.subTest(depth=depth, selected_filter=selected_filter):
                    found = set()
                    for directory_name in directories:
                        if not (root / directory_name).is_dir():
                            continue
                        command = ["find", directory_name]
                        if directory_name == ".":
                            command += ["-maxdepth", "1"]
                        else:
                            command += ["-maxdepth", str(depth)]
                        command += ["(", "-iname", "*.c", "-o", "-iname",
                                    "*.h", ")"]
                        result = subprocess.run(
                            command, cwd=root, capture_output=True,
                            text=True, check=True)
                        found.update(result.stdout.splitlines())
                    ryu = subprocess.run(
                        ["find", "cbase/ryu", "-type", "f", "(",
                         "-iname", "*.c", "-o", "-iname", "*.h", ")"],
                        cwd=root, capture_output=True, text=True, check=True)
                    found.update(ryu.stdout.splitlines())
                    expected = set()
                    for raw in found:
                        path = raw.removeprefix("./")
                        parts = path.split("/")
                        if "stc" in parts[:-1] or path.endswith(".meta.h"):
                            continue
                        name = parts[-1]
                        if selected_filter and name.endswith(".c"):
                            module = name[:-2]
                            if name != selected_filter and module != selected_filter:
                                if selected_filter != "ryu" or not (
                                    path.startswith("cbase/ryu/")
                                    and len(parts) == 3):
                                    continue
                        expected.add(path)
                    result = run_checker([
                        "--root", root, "--max-depth", str(depth),
                        "--filter", selected_filter, "--coverage"], root)
                    self.assertIn(result.returncode, (0, 1), result.stderr)
                    actual = {
                        str(Path(path).relative_to(root)) for path in
                        re.findall(r"^coverage file=(.*?) declarations=",
                                   result.stdout, re.MULTILINE)
                    }
                    self.assertEqual(actual, expected, result.stdout)

    def test_symlinked_top_level_source_directory_is_not_walked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "project"
            root.mkdir()
            vendor = Path(directory) / "vendor"
            write_file(vendor, "vendored.c", BAD_SOURCE)
            (root / "src").symlink_to(vendor, target_is_directory=True)
            self.assert_files(["--root", root], root, 0)
            self.assert_files(["--file", root / "src/vendored.c"], root, 1)

    def test_symlinked_ryu_directory_is_not_walked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "project"
            root.mkdir()
            write_file(root, "cbase/ordinary.c", BAD_SOURCE)
            vendor = Path(directory) / "vendor"
            write_file(vendor, "vendored.c", BAD_SOURCE)
            (root / "cbase/ryu").symlink_to(vendor, target_is_directory=True)
            self.assert_files(["--root", root], root, 1)

    def test_symlink_directories_are_not_followed_in_discovery(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = write_file(root, "src/ordinary.c", BAD_SOURCE)
            (root / "tools/link").parent.mkdir(parents=True)
            (root / "tools/link").symlink_to(root / "src", target_is_directory=True)
            self.assert_files(["--root", root], root, 1)
            self.assert_files(["--file", root / "tools/link/ordinary.c"],
                              root, 1)
            self.assertTrue(original.is_file())


if __name__ == "__main__":
    unittest.main()
