# Third-party attribution

The C4MTB project uses [roBa](https://github.com/kumamuk-git/roBa) by kumamuk as its implementation base (GPL-3.0). The key layout was developed with reference to [Corne V4 (crkbd)](https://github.com/foostan/crkbd) by foostan (MIT / CC BY 4.0).

This repository contains the C4MTB ZMK firmware configuration, keymap and editor layout. The C4MTB-specific files are licensed under GPL-3.0. See [LICENSE](LICENSE). Upstream material retains its respective attribution and license conditions.

## Firmware dependencies

The ZMK and Zephyr forks and Studio modules are maintained by [cormoran](https://github.com/cormoran) and their upstream contributors. The firmware configuration references [zmk-config-dya-studio-sample](https://github.com/cormoran/zmk-config-dya-studio-sample) at `e4ab61d5797e9216c1f66cac02562caa7acc3d4f`. West fetches the dependencies listed in [config/west-dependency.yml](config/west-dependency.yml) under their respective upstream licenses, including [zmk-scroll-snap](https://github.com/iwmsl/zmk-scroll-snap) at `v1`.

## PMW3610 recovery patch

[The patch](patches/pmw3610-recovery.patch) adapts `src/pmw3610.c` and `src/pixart.h` from [cormoran's PMW3610 driver](https://github.com/cormoran/zmk-driver-pmw3610-with-custom-studio-rpc) at `5c34ea0eec246a1c986111417cd779b53144629a`. It adds serialized SPI access, error handling and periodic recovery. Copyright (c) 2022 The ZMK Contributors. See the [upstream MIT license](patches/PMW3610-LICENSE).
