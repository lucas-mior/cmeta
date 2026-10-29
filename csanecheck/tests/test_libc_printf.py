#!/usr/bin/env python3
"""CG039: forbid direct libc printf-family calls outside cbase."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(
    r":(\d+):\d+: error\[API-LIBC-PRINTF\]: (.*)"
)
MESSAGE = "use cbase formatting instead of libc printf"


class LibcPrintfTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, relative, source, expected_lines):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--file", str(path)], cwd=ROOT,
            capture_output=True, text=True, check=False,
        )
        found = [(int(line), message) for line, message in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(
            found, [(line, MESSAGE) for line in expected_lines],
            result.stdout + result.stderr,
        )
        return path

    def test_libc_families(self):
        names = (
            "printf", "fprintf", "sprintf", "snprintf", "vprintf",
            "vfprintf", "vsprintf", "vsnprintf", "dprintf", "vdprintf",
            "asprintf", "vasprintf", "wprintf", "fwprintf", "swprintf",
            "vwprintf", "vfwprintf", "vswprintf",
        )
        source = "void run(void) {\n"
        for name in names:
            source += f'    {name}("test");\n'
        source += "}\n"
        self.check("src/formatting.c", source,
                   list(range(2, len(names) + 2)))

    def test_parenthesized_nested_macros_and_preprocessor(self):
        self.check("src/macros.c", """#define BAD(v) fprintf(stderr, "%d", v)
#define PASS(printf) printf
#if defined(FEATURE)
void a(void) { (printf)("text"); }
#else
void b(void) { use(snprintf(buffer, size, "%d", n)); }
#endif
#define ALSO_BAD(...) asprintf(__VA_ARGS__)
void c(void) { fprintf
                     (stderr, "test"); }
""", [1, 4, 6, 8, 9])

    def test_valid_wrappers_and_non_calls(self):
        self.check("src/allowed.c", """int32 printf(char *format, ...);
int32 fprintf(FILE *file, char *format, ...);
static int32 sprintf(char *format) { return 1; }
#define printf(...) SOME_OTHER_MACRO(__VA_ARGS__)
void run(void) {
    fmt_printf("hello");
    fmt_snprintf(buffer, count, "hello");
    str_printf(&string, "hello");
    strflex_list_printf(&list, "hello");
    SNPRINTF(buffer, "hello");
    error2("hello");
    object.printf("hello");
    object->fprintf(stderr, "hello");
    // printf("commented");
    char *text = "printf(quoted)";
    int32 n = sizeof(printf);
}
""", [])

    def test_cbase_implementation_exemption(self):
        self.check("cbase/backend.c", """void f(void) {
    printf("text");
    fprintf(stderr, "text");
    vsnprintf(buffer, size, format, args);
}
#define LOG(...) fprintf(stderr, __VA_ARGS__)
""", [])
        self.check("src/backend.c", """void f(void) {
    printf("text");
    fprintf(stderr, "text");
}
""", [2, 3])

    def test_prefixes_and_similarly_named_calls_are_not_libc(self):
        self.check("src/names.c", """void run(void) {
    my_printf("text");
    printf2("text");
    fmt_vprintf("text");
    DEBUG_PRINTF("text");
    fprintf_wrapper(stderr, "text");
}
""", [])


if __name__ == "__main__":
    unittest.main()
