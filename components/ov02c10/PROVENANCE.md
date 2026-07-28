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

One further deviation, in the 1288x728 format register table
(`private_include/ov02c10_mipi_1lane_24Minput_1288x728_raw10_30fps.h`):

3. **Black-level target `0x4003`: `0x40` → `0x00`.** The vendor value adds a
   pedestal of 64/1024 (≈16/255) to every pixel. Nothing downstream removes it —
   the ESP32-P4 ISP has a black-level block, but ESP-IDF 5.5.1 ships no driver for
   it and `esp_video` exposes no control — so it reaches the output, where the
   auto-white-balance gains scale it **per channel** and the gamma curve expands
   what survives. Measured on device: +27/255 residual on red and +45/255 on blue,
   with highlights still neutral (R/G 0.950, B/G 1.020) — i.e. shadows rendered as
   washed navy while the white balance itself was correct. A single global black
   point cannot cancel a per-channel offset, so it is removed at source. With this
   change all three channels floor at exactly 0.000. Evidence:
   `docs/guition-camera-followups.md` §1.

## IPA tuning file — `cfg/ov02c10_seedsigner_p4_eco4.json`

Derived from the vendor's `cfg/ov02c10_default_p4_eco4.json` in the same drop.
The vendor ships one tuning file per silicon class and picks between them with
`CONFIG_ESP32P4_SELECTS_REV_LESS_V3`: below v3 → `eco4`, v3 and later → `eco5`.
The eco5 file additionally configures lens-shading and black-level correction
plus a colour-temperature-interpolated CCM. Only the eco4 lineage is carried
here; the target part is an ESP32-P4 v1.0.

The file is verbatim from the vendor except for five deliberate deltas:

1. **Sensor key renamed `OV02C10` → `OV02C10_SS`.** `esp_video_init()` looks up
   an IPA config by the sensor's own name and, on a hit, creates the ISP/IPA
   pipeline itself using the (flash-resident, `const`) config. Renaming the key
   makes that lookup miss so `board_pipeline_camera_csi.c` can create the
   pipeline instead, backed by a RAM copy of the AGC block it can retune per
   camera session. Boards declare the key they want via
   `BOARD_CAMERA_IPA_CONFIG_NAME`.
2. **`awb.min_red_gain_step` / `min_blue_gain_step` 0.34 / 0.4 → 0.034.** The
   vendor numbers are tuned for a newer `esp_ipa` whose AWB also consumes the
   `model` / `range` / `green_luma_*` keys. The pinned `esp_ipa` 0.2.0 runs a
   simpler white-patch algorithm for which 0.34 is a very coarse update floor;
   0.034 is what Espressif ships for that algorithm's own sensors.
3. **`agc.anti_flicker.mode` `full` → `none`.** Anti-flicker constrains every
   exposure to a whole number of mains half-periods — 10 ms at 50 Hz. This format
   runs `vts=1164` at 30 fps (28.64 µs per line), so the exposure loop's entire
   usable range collapses to ~10 / 20 / 30 ms, all long enough to smear a
   hand-held QR code, and it measurably overexposed a lamp-lit scene by settling
   on 20 ms (device-observed `set exposure 0x2ba` = 698 lines = 19.99 ms). `part`
   was tried first and is NOT a middle ground: it still forces the quantised
   exposure whenever gain has any room to move, which is nearly always. The cost
   of `none` is possible banding under AC lighting.
4. **`agc.luma_adjust.weight` flattened to 25 × 3.** The board switches metering
   tables per camera session and they must all sum alike; see below.
5. **`acc`, `adn` and `aen` blocks removed** — colour matrix + saturation, denoise +
   demosaic, and gamma + sharpen + contrast respectively. Device-measured, they add a
   fixed black-level pedestal (~0.21 of full scale) that no scene or exposure changes,
   crushing the image into a ~20-level band and giving AWB a tinted black to amplify.
   Only `agc` (exposure/gain) and `awb` (white balance) are kept — the two loops this
   sensor actually lacks in hardware. Evidence and numbers:
   `docs/guition-camera-followups.md` §1.

The vendor keys that `esp_ipa` 0.2.0 does **not** read are left in place so a
future component bump picks them up: `awb.model`, `awb.range`,
`awb.green_luma_env` / `green_luma_init` / `green_luma_step_ratio`, and
`agc.f_m0`. They are inert today — do not treat them as active tuning.

⚠ **The metering weight table's sum is load-bearing.** `esp_ipa`'s AGC caches
`sum(weight)` once at pipeline init and re-reads the table itself every frame,
so any table swapped in at runtime must sum to the same 75. See
`docs/knowledge/esp32-p4-ipa-adaptive-ae-awb-ov02c10.md`.

## Re-syncing from a newer vendor drop

Diff the new `ov02c10.c` against this one, re-apply the two backports (or drop
them if `esp_cam_sensor` has been bumped past 2.0.0 by then), and refresh the
headers under `include/` and `private_include/` verbatim. For the tuning file,
re-apply the three deltas above to the new `ov02c10_default_p4_eco4.json`.
