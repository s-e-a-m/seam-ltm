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

An S-format plugin — five fine controls or fewer, whatever the plugin does
with them — starts from the same file.
Narrow the frame to `300, N`, keep one column at x=20 of width 260, move the
right column's blocks under the left one, and widen the type scale to
`KnobLabelFont` 13 and `ValueFont` 12, which is what the 300 px windows use.
Keep the SETUP and OPS zones if the plugin has them: a STONE selector and a
POWER switch cost one row each and fit a single column, as multipink shows.
Drop the column headers, which name a group of one once there is only one
column.

The skeleton is L because two columns are the harder shape to lay out from
scratch, and narrowing is the smaller edit.
