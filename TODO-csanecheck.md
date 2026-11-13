# Prompt:
Implement the first item below, then remove it from this list.

1. Add a check for:
   ```c
   // bad
   ASSERT(STREQUAL(string, string_len, "string"));
   ASSERT(STREQUAL(string, string_len, other, other_len));

   // good
   ASSERT_EQ(string, string_len, "string");
   ASSERT_EQ(string, string_len, other, other_len);
   ```
