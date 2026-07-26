# OV02C10 driver provenance

OV02C10 is a 2 MP OmniVision MIPI-CSI sensor used on the Guition JC4880P443
ESP32-P4 board. It is **not** in the public `espressif/esp_cam_sensor` component
(no version, up to and including 2.3.0, ships an `ov02c10` sensor — the registry
has `os02n10` / `os04c10` but not `ov02c10`). The only source is Espressif's
driver as redistributed in the board vendor's SDK package.

## Source

Vendor package `JC4880P443C_I_W.zip` (gitignored, kept at the builder repo root):

    1-Demo/idf_examples/components/esp_cam_sensor/sensors/ov02c10/

That copy targets `esp_cam_sensor` **2.1.0**. This project pins `esp_cam_sensor`
**0.9.0** (see `ports/esp32/board_common/idf_component.yml`), so the driver is
carried here as a standalone add-on component rather than by bumping the shared
sensor component (which would also move the OV5647 release target and the
`esp_video` 0.8.x V4L2 layer — large blast radius for no gain here).

Files are verbatim from the vendor drop except `ov02c10.c`, which has two small
backports to the 0.9.0 API (each marked inline with an `esp_cam_sensor 0.9.0:`
comment):

1. `esp_cam_sensor_isp_info_v1_t` gained a `tline_ns` member only in 2.x. The
   three per-format `.tline_ns` designated initializers are removed. `tline_ns`
   is an IPA line-time hint; the ISP still demosaics without it.
2. `esp_cam_sensor_gh_exp_gain_t` gained an `exposure_val` member only in 2.x.
   The `ESP_CAM_SENSOR_GROUP_EXP_GAIN` handler drops the `exposure_val` branch
   and uses `exposure_us` exclusively.

Not copied: the vendor `cfg/ov02c10_default_p4_eco*.json` IPA tuning files. Like
the OV5647 target, this build ships no IPA JSON (`ESP_IPA_JSON_CONFIG_FILE_PATH`
is unset), so `esp_video`'s ISP pipeline controller runs with algorithm
defaults. AE/AWB tuning is a later refinement.

## Re-syncing from a newer vendor drop

Diff the new `ov02c10.c` against this one, re-apply the two backports (or drop
them if `esp_cam_sensor` has been bumped past 2.0.0 by then), and refresh the
headers under `include/` and `private_include/` verbatim.
