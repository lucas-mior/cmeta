# Prompt:
Implement the first item below, then remove it from this list.

1. Add a check to csanecheck and corresponding guideline to cbase/c-guidelines.md
   prohibiting:
   ```c
   fmt_sprintf(name, SIZEOF(name), ...);
   ```
   * The correct pattern is: SNPRINTF(name, ...);
   * fmt_sprintf is not to be called directly in stack/static arrays.
2. Add a check for:
   ```c
   // bad
   ASSERT(STREQUAL(string, string_len, "string"));
   ASSERT(STREQUAL(string, string_len, other, other_len));

   // good
   ASSERT_EQ(string, string_len, "string");
   ASSERT_EQ(string, string_len, other, other_len);
   ```
