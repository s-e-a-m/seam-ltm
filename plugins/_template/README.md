# Plugin skeleton

`resource/_template.uidesc` is an L-format starting point that already
conforms to `doc/style/ui-style.md`: all five zones traced, canonical
palette, all-white text, two-column FINE geometry.

This directory is **not** registered in the root `CMakeLists.txt`.
Nothing here builds; it exists to be copied.

To start a plugin:

1. `cp -r plugins/_template plugins/<name>` and rename the `.uidesc`.
2. Change the title to `SEAM <NAME>` with `<NAME>` uppercased — the lint
   checks it against the directory name.
3. Delete the zones the plugin does not have.
   Do not reorder the rest.
4. Run `python3 tools/check-uidesc.py plugins/<name>/resource/<name>.uidesc`
   before writing any C++.
5. Add `add_subdirectory(plugins/<name>)` to the root `CMakeLists.txt`.

An S-format plugin (a passive converter, up to about four controls) starts
from the same file: narrow the template to `300, N`, drop the SETUP and OPS
zones, and keep one column.
