# Prompt
Implement the first item of the list below, then remove it from the list.

1. Add a check prohibiting identifier names that are more than 4 "namespaces"
   deep: For instance:
   ```c
   // bad (4 underscores, 5 names)
   int32 system_subsystem_variable_name_long;
   ```

2. Add a check prohibiting accessing members 5 levels deep or more. Use aliases.
   ```c
   // bad 
   int32 value = top_level->med_level.low.lower->value;

   // good 
   MyStruct *s = top_level->med_level.low;
   int32 value = s->lower->value;
   ```

2. Add a check prohibiting combining members + namespaces deepth of final
   identifier to be 6 or more. Change the variable name
   ```c
   // bad
   int32 value = top_level->med_level.low.value_name_space;

   // good 
   int32 value = top_level->med_level.low.value;
   ```
