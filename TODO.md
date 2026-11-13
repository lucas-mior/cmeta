# TODO
- Relax the FMT-STRING-SPLIT check so that it allows spliting when the string
  literal fragment ends with a new line:
  ```c
  // allowed
  char *body = "double a;\n"
               "double b = a + position;\n"
               "xdot[0] = b;\n";

  // not allowed
  char *body = "double a;"
               "double b = a + position;"
               "xdot[0] = b;";
  ```

- Relax the FMT-STRING-SPLIT check so that string literal fragments without
  formatters don't try to mix themselves with format strings:
  // good, no problem
  error("Consistency error:"
        " %s.row_ids["RED("%d")"] -> row_id="YELLOW("%d")","
        " but %s["YELLOW("%d")"] -> idx="RED("%d")".\n",
        which_traversal, idx, row_id, which_rows, row_id, rows[row_id]);

  // this must be flagged (note how *the complete literal fits*)
  error("Consistency error:"
        " %s.row_ids["RED("%d")"] -> row_id="YELLOW("%d")","
        which_traversal, idx, row_id);
  // corrected form:
  error("Consistency error: %s.row_ids["RED("%d")"] -> row_id="YELLOW("%d")","
        which_traversal, idx, row_id);

- Add a check to csanecheck and corresponding guideline to cbase/c-guidelines.md
  prohibiting:
  ```c
  fmt_sprintf(name, SIZEOF(name), ...);
  ```
  * The correct pattern is: SNPRINTF(name, ...);
  * fmt_sprintf is not to be called directly in stack/static arrays.
- Add a check for:
  ```c
  // bad
  ASSERT(STREQUAL(string, string_len, "string"));
  ASSERT(STREQUAL(string, string_len, other_other_len));

  // good
  ASSERT_EQ(string, string_len, "string");
  ASSERT_EQ(string, string_len, other_other_len);
  ```
