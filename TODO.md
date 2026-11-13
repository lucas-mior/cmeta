# TODO
- Add a check to csanecheck and corresponding guideline to cbase/c-guidelines.md
  prohibiting:
  ```c
  fmt_sprintf(name, SIZEOF(name), ...);
  ```
  * The correct pattern is: SNPRINTF(name, ...);
  * fmt_sprintf is not to be called directly in stack/static arrays.
