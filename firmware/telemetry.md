# Privacy-preserving usage telemetry

The right half is the BLE central, so it can aggregate events from both halves
without installing software on the host PC. The normal `c4mtb_R` firmware stores
only cumulative counters in the keyboard's internal settings partition.

## Stored data

- Press counts for each of the 38 physical positions, split across all six layers
- Layer activation counts
- Mouse button 1–3 press counts
- Absolute pointing distance and axis-event counts for cursor, scroll, and zoom modes
- Format version, save count, and saturation/overflow count

It does **not** store characters, keycodes, press order, timestamps, hold duration,
application/window names, clipboard data, or anything received from the host.
The counters cannot reconstruct typed text.

Counts are kept in RAM and written at most once every 30 minutes while activity
continues. Each changed layer or metric group is stored separately, so unchanged
groups are not rewritten and individual settings records stay small. Normal UF2
updates preserve the settings partition; flashing
`settings_reset` erases telemetry together with the other ZMK settings.

## Export on a personal computer

1. Download and flash `c4mtb_R_telemetry_export` on the right half.
2. Open the USB serial port at 115200 baud.
3. Capture the JSON between `C4MTB_TELEMETRY_BEGIN` and
   `C4MTB_TELEMETRY_END`. A snapshot repeats every five seconds.
4. Flash the normal `c4mtb_R` artifact again before returning to normal use.

The export firmware is deliberately separate. The normal firmware never sends
telemetry to a host, over BLE, or over USB. Analysis should compare two cumulative
snapshots; the firmware does not retain time periods itself.

## Versioning and interpretation

The current format version is 1. Layer indices follow the keymap order:

| Index | Layer |
| ---: | --- |
| 0 | Default |
| 1 | Number |
| 2 | Navigation |
| 3 | Scroll |
| 4 | Settings |
| 5 | Zoom |

Pointer mode indices are cursor (0), scroll (1), and zoom (2). Distances are sums
of absolute relative-axis values before the scroll/zoom snap and scaling stages,
so they are useful for comparing workload but are not physical millimetres.

If the number of layers or physical positions changes, the firmware build fails
until this format is reviewed. An incompatible stored format is ignored and
reinitialized rather than being misinterpreted.
