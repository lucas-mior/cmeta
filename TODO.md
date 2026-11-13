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
- Add a check to csanecheck and corresponding guideline to cbase/c-guidelines.md
  prohibiting:
  ```c
  fmt_sprintf(name, SIZEOF(name), ...);
  ```
  * The correct pattern is: SNPRINTF(name, ...);
  * fmt_sprintf is not to be called directly in stack/static arrays.
