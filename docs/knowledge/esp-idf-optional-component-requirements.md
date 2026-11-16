# ESP-IDF: optional features belong in separate components

esp-board-common's camera support lives in its own component, `components/board_camera`,
instead of behind a switch in the core component. This doc covers the ESP-IDF
build-system behavior behind that choice. It applies to any component with a feature
that some consumers don't want. Checked against ESP-IDF v5.5 and idf-component-manager
2.5.

## A switch can't keep a managed dependency from being fetched

The component manager reads the `idf_component.yml` of every component on the search
path and downloads what each one declares. That happens before ESP-IDF works out which
components are actually required, so a dependency is fetched whenever the manifest that
declares it is on the search path:

- `require: no` only keeps the dependency out of the owning component's REQUIRES. It
  is still downloaded.
- A `rules: - if:` clause can test `target`, `idf_version`, an environment variable
  (`$VAR`), or a Kconfig option (`$CONFIG{...}`). A build property is invisible to it.
  An environment variable that is not set is a fatal error
  (`Environment variable "VAR" is not set`), so an opt-out that defaults to on would
  force every consumer to set the variable.
- `MINIMAL_BUILD` doesn't help either: the build is trimmed to `main` and its
  requirements only after the downloads are done.

Rule: put an optional feature's managed dependencies in the manifest of a component that
holds only that feature. A consumer that doesn't want the feature keeps that component
off its search path, and the dependencies are never fetched.

## Every component on the search path is built

Without `MINIMAL_BUILD` (or an explicit `COMPONENTS` list), ESP-IDF builds every
component it discovers, required or not, and passes all of them to the linker. That
costs more than build time: not everything in an unused component is discarded at link
time. On ESP32-P4, leaving the unrequired camera drivers in a camera-less build added
about 32 KB to the firmware. A consumer that wants one component from a
directory of optional components should therefore list that component's directory
alone. Listing the parent directory pulls in every sibling, along with the dependencies
their manifests declare.

The same applies to a component whose requirements exist only on some targets. On
another target, it should register as an empty component so that having its directory
on the search path is harmless. `components/ov02c10` does this: it needs
`esp_cam_sensor`, which is fetched only for ESP32-P4.

## REQUIRES are collected in a script-mode pass

Before the normal configure pass, `idf_build_process()` collects every component's
REQUIRES by including each `CMakeLists.txt` in a separate `cmake -P` process, with
`CMAKE_BUILD_EARLY_EXPANSION` set and a stubbed `idf_component_register()`. That pass
sees build properties (including `IDF_TARGET`), environment variables, and
`COMPONENT_DIR`. It does not see the project's `-D` cache variables or Kconfig. Anything
a component's REQUIRES depend on must come from the first group, or the two passes
disagree.

`board_camera` relies on this to find the core component's name. A component's name is
its directory name, and consumers check esp-board-common out under different names
(`esp-board-common` in its own apps, `board_common` in the SeedSigner MicroPython
builder). `board_camera` takes the name from `COMPONENT_DIR/../..`, which both passes
can see.
