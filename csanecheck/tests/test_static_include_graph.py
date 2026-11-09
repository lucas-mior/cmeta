#!/usr/bin/env python3
"""Standalone Step 6 coverage for physical-source inclusion discovery."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''#define main csanecheck_original_main
#include "csanecheck/main.c"
#undef main

int
main(int argc, char **argv) {
    CsanePaths paths = {0};
    CsaneIncludeGraph graph = {0};

    for (int32 i = 1; i < argc; i += 1) {
        csane_paths_add(&paths, argv[i], strlen32(argv[i]));
    }
    if (!csane_include_graph_build(&graph, &paths)) {
        return 2;
    }
    for (int32 i = 0; i < ARRAY_LEN(graph.nodes); i += 1) {
        CsaneIncludeNode *node = &graph.nodes[i];

        fmt_printf("NODE\t%d\t%s\t%d\n", i, node->path, node->uncertain);
        for (int32 j = 0; j < ARRAY_LEN(node->edges); j += 1) {
            CsaneIncludeEdge *edge = &node->edges[j];

            fmt_printf("EDGE\t%d\t%d\t%d\t%d\n", i, edge->target,
                       edge->conditional, edge->unresolved);
        }
    }
    for (int32 i = 1; i < argc; i += 1) {
        int32 index = csane_include_graph_find(&graph, argv[i]);

        fmt_printf("ROOT\t%d\t%d\n", i - 1, index);
    }
    for (int32 i = 0; i < ARRAY_LEN(graph.nodes); i += 1) {
        for (int32 j = 0; j < ARRAY_LEN(graph.nodes); j += 1) {
            CsaneIncludeReachability reach =
                csane_include_graph_reach(&graph, i, j);

            fmt_printf("REACH\t%d\t%d\t%d\t%d\t%d\n", i, j,
                       reach.reachable, reach.unconditional,
                       reach.uncertain);
        }
    }
    csane_include_graph_free(&graph);
    csane_paths_free(&paths);
    return 0;
}
'''


class IncludeGraphTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build_dir = tempfile.TemporaryDirectory()
        harness = Path(cls.build_dir.name) / "include_graph_probe.c"
        cls.executable = Path(cls.build_dir.name) / "include_graph_probe"
        harness.write_text(HARNESS, encoding="utf-8")
        subprocess.run(
            [os.environ.get("CC", "cc"), "-std=c11", "-O0", "-I", str(ROOT),
             "-I", str(ROOT / "cbase"), str(harness), "-lm", "-o",
             str(cls.executable)],
            cwd=ROOT, check=True, capture_output=True, text=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls.build_dir.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)

    def graph(self, sources, roots):
        for name, content in sources.items():
            path = self.base / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        command = [str(self.executable)]
        command += [str(self.base / name) for name in roots]
        result = subprocess.run(command, cwd=ROOT, check=True,
                                capture_output=True, text=True)
        nodes = {}
        edges = []
        reaches = {}
        root_indices = []
        for line in result.stdout.splitlines():
            fields = line.split("\t")
            if fields[0] == "NODE":
                nodes[int(fields[1])] = (
                    str(Path(fields[2]).relative_to(self.base)),
                    bool(int(fields[3])),
                )
            elif fields[0] == "EDGE":
                edges.append(tuple(map(int, fields[1:])))
            elif fields[0] == "REACH":
                values = list(map(int, fields[1:]))
                reaches[tuple(values[:2])] = tuple(map(bool, values[2:]))
            elif fields[0] == "ROOT":
                root_indices.append(int(fields[2]))
        return nodes, edges, reaches, root_indices

    def test_direct_include_and_physical_file_identity(self):
        nodes, edges, reaches, roots = self.graph({
            "src/owner.c": '#include "child.c"\n',
            "src/child.c": "int child(void) { return 1; }\n",
        }, ["src/owner.c", "src/child.c", "src/./child.c"])
        self.assertEqual(len(nodes), 2)
        self.assertEqual(roots[1], roots[2])
        self.assertIn((roots[0], roots[1], 0, 0), edges)
        self.assertEqual(reaches[roots[0], roots[1]], (True, True, False))

    def test_transitive_includes_through_header(self):
        nodes, edges, reaches, roots = self.graph({
            "a.c": '#include "sub/middle.h"\n',
            "sub/middle.h": '#include "../b.c"\n',
            "b.c": "int b(void) { return 1; }\n",
        }, ["a.c", "b.c"])
        self.assertEqual(len(nodes), 3)
        self.assertEqual(reaches[roots[0], roots[1]], (True, True, False))
        self.assertFalse(reaches[roots[1], roots[0]][0])

    def test_conditional_and_unconditional_paths(self):
        nodes, edges, reaches, roots = self.graph({
            "a.c": '#if FEATURE\n#include "b.c"\n#endif\n',
            "b.c": "int b(void) { return 1; }\n",
        }, ["a.c", "b.c"])
        self.assertIn((roots[0], roots[1], 1, 0), edges)
        self.assertEqual(reaches[roots[0], roots[1]], (True, False, False))

    def test_nested_conditional_path(self):
        nodes, edges, reaches, roots = self.graph({
            "a.c": '#if FEATURE\n#include "mid.c"\n#endif\n',
            "mid.c": '#include "target.c"\n',
            "target.c": "int target(void) { return 2; }\n",
        }, ["a.c", "target.c"])
        self.assertEqual(reaches[roots[0], roots[1]], (True, False, False))

    def test_include_cycles_terminate(self):
        nodes, edges, reaches, roots = self.graph({
            "a.c": '#include "b.c"\n',
            "b.c": '#include "a.c"\n',
        }, ["a.c", "b.c"])
        self.assertEqual(len(nodes), 2)
        self.assertEqual(reaches[roots[0], roots[0]], (True, True, False))
        self.assertEqual(reaches[roots[1], roots[0]], (True, True, False))

    def test_missing_quoted_c_include_is_unresolved(self):
        nodes, edges, reaches, roots = self.graph({
            "a.c": '#include "missing.c"\n',
        }, ["a.c"])
        self.assertEqual(len(nodes), 1)
        self.assertIn((roots[0], -1, 0, 1), edges)
        self.assertTrue(nodes[roots[0]][1])

    def test_angle_bracket_c_include_is_unresolved(self):
        nodes, edges, reaches, roots = self.graph({
            "a.c": '#include <external.c>\n',
        }, ["a.c"])
        self.assertTrue(nodes[roots[0]][1])
        self.assertEqual(len(edges), 0)

    def test_regular_angle_bracket_header_is_not_c_inclusion(self):
        nodes, edges, reaches, roots = self.graph({
            "a.c": '#include <stdio.h>\n',
        }, ["a.c"])
        self.assertFalse(nodes[roots[0]][1])

    def test_multiple_paths_include_one_unconditional(self):
        nodes, edges, reaches, roots = self.graph({
            "a.c": '#if FEATURE\n#include "b.c"\n#endif\n'
                   '#include "b.c"\n',
            "b.c": "int b(void) { return 1; }\n",
        }, ["a.c", "b.c"])
        self.assertEqual(len(nodes), 2)
        self.assertEqual(reaches[roots[0], roots[1]], (True, True, False))

    def test_missing_header_records_uncertainty(self):
        nodes, edges, reaches, roots = self.graph({
            "a.c": '#include "missing.h"\n',
        }, ["a.c"])
        self.assertTrue(nodes[roots[0]][1])
        self.assertEqual(len(edges), 0)

    def test_macro_include_is_uncertain(self):
        nodes, edges, reaches, roots = self.graph({
            "a.c": '#define SOURCE "b.c"\n#include SOURCE\n',
            "b.c": "int b(void) { return 1; }\n",
        }, ["a.c", "b.c"])
        self.assertTrue(nodes[roots[0]][1])
        self.assertFalse(reaches[roots[0], roots[1]][0])
        self.assertTrue(reaches[roots[0], roots[1]][2])

    def test_unrelated_string_or_comment_is_not_an_include(self):
        nodes, edges, reaches, roots = self.graph({
            "a.c": '// #include "b.c"\nchar *text = "#include b.c";\n',
            "b.c": "int b(void) { return 1; }\n",
        }, ["a.c", "b.c"])
        self.assertEqual(len(edges), 0)
        self.assertFalse(reaches[roots[0], roots[1]][0])

    def test_unrelated_translation_units_do_not_merge(self):
        nodes, edges, reaches, roots = self.graph({
            "a.c": "static int same(void) { return 1; }\n",
            "b.c": "static int same(void) { return 2; }\n",
        }, ["a.c", "b.c"])
        self.assertEqual(len(nodes), 2)
        self.assertFalse(reaches[roots[0], roots[1]][0])

    @unittest.skipUnless(os.name == "posix", "symlinks require POSIX")
    def test_symlink_aliases_are_same_physical_source(self):
        (self.base / "actual.c").write_text("int actual;\n", encoding="utf-8")
        (self.base / "alias.c").symlink_to("actual.c")
        nodes, edges, reaches, roots = self.graph({}, ["actual.c", "alias.c"])
        self.assertEqual(len(nodes), 1)
        self.assertEqual(roots[0], roots[1])


if __name__ == "__main__":
    unittest.main()
