#!/usr/bin/env python3
"""ZMK shieldがC4MTBのピン定義とエディター用レイアウトに一致することを検証する。

  - 左右で異なる5本目の列GPIO
  - 右手PMW3610のSCLK/SDIO/CS/MOTION
  - splitのcol-offsetと左右非対称Row3 matrix transform
  - physical layout/keymapの38キーからのずれ
  - physical layoutの座標・回転とconfig/info.jsonの不一致
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHIELD = ROOT / "boards" / "shields" / "c4mtb"
LAYERS = (
    "default_layer",
    "number_layer",
    "navigation_layer",
    "scroll_layer",
    "settings_layer",
    "zoom_layer",
)


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def strip_comments(source: str) -> str:
    """DTS/keymapのコメントを落とす。

    無効化した設定をコメントで残すのは普通なので、コメント内の`invert-y;`や
    `&zip_temp_layer`を有効な記述と誤検出しないようにする。
    """
    source = re.sub(r"/\*.*?\*/", " ", source, flags=re.S)
    return re.sub(r"(?<!:)//[^\n]*", " ", source)


def node_block(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\{{", source)
    if not match:
        raise ValueError(f"node not found: {name}")
    start = source.find("{", match.start())
    depth = 0
    for pos in range(start, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1:pos]
    raise ValueError(f"unterminated node: {name}")


def gpio_list(source: str) -> list[str]:
    block = node_block(source, "kscan0")
    return re.findall(r"<&(?:xiao_d|gpio0)\s+\d+", block)


def physical_layout_failures(dtsi: str, layout: list[dict]) -> list[str]:
    """エディター用のkey-unitをZMKのcenti-key-unitに変換して照合する。"""
    entries = [
        [int(value.strip("()")) for value in row]
        for row in re.findall(
            r"&key_physical_attrs\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+"
            r"(\(?-?\d+\)?)\s+(\d+)\s+(\d+)",
            dtsi,
        )
    ]
    if len(entries) != len(layout):
        return [f"physical layoutキー数: {len(entries)} != {len(layout)}"]

    failures: list[str] = []
    for index, (key, entry) in enumerate(zip(layout, entries)):
        want = [
            round(key.get(attribute, default) * 100)
            for attribute, default in (("u", 1), ("h", 1), ("x", 0), ("y", 0),
                                       ("r", 0), ("rx", 0), ("ry", 0))
        ]
        if entry != want:
            failures.append(
                f"physical layout[{index}]: {entry} != {want} (w h x y rot rx ry)"
            )
    return failures


def main() -> int:
    failures: list[str] = []
    info = json.loads(text(ROOT / "config" / "info.json"))
    layout = info["layouts"]["default_transform"]["layout"]
    dtsi = strip_comments(text(SHIELD / "c4mtb.dtsi"))
    left = strip_comments(text(SHIELD / "c4mtb_l.overlay"))
    right = strip_comments(text(SHIELD / "c4mtb_r.overlay"))
    keymap = strip_comments(text(ROOT / "config" / "c4mtb.keymap"))
    right_conf = text(ROOT / "config" / "c4mtb_r.conf")
    dya_conf = text(
        ROOT / "snippets" / "c4mtb-dya-studio" / "c4mtb-dya-studio.conf"
    )

    row_gpio = re.search(r"row-gpios(.*?);", dtsi, re.S)
    actual_rows = re.findall(r"<&xiao_d\s+\d+", row_gpio.group(1)) if row_gpio else []
    expected_rows = ["<&xiao_d 1", "<&xiao_d 2", "<&xiao_d 3", "<&xiao_d 6"]
    if actual_rows != expected_rows:
        failures.append(f"Row GPIO: {actual_rows} != {expected_rows}")

    expected_left = [
        "<&xiao_d 10", "<&xiao_d 9", "<&xiao_d 8", "<&xiao_d 7", "<&xiao_d 5",
    ]
    expected_right = [
        "<&xiao_d 10", "<&xiao_d 9", "<&xiao_d 8", "<&xiao_d 7", "<&gpio0 10",
    ]
    if gpio_list(left) != expected_left:
        failures.append(f"L Col GPIO: {gpio_list(left)} != {expected_left}")
    if gpio_list(right) != expected_right:
        failures.append(f"R Col GPIO: {gpio_list(right)} != {expected_right}")

    compact_right = re.sub(r"\s+", "", right)
    required_right = {
        "split col-offset=5": "col-offset=<5>;",
        "SCLK=P0.05": "NRF_PSEL(SPIM_SCK,0,5)",
        "SDIO MOSI=P0.04": "NRF_PSEL(SPIM_MOSI,0,4)",
        "SDIO MISO=P0.04": "NRF_PSEL(SPIM_MISO,0,4)",
        "CS=P0.09": "cs-gpios=<&gpio09GPIO_ACTIVE_LOW>;",
        "MOTION=P0.02": "irq-gpios=<&gpio02(GPIO_ACTIVE_LOW|GPIO_PULL_UP)>;",
        "DYA PMW3610 driver": 'compatible="cormoran,pmw3610";',
        "PMW3610 settings ID": 'settings-id="trackball";',
    }
    for label, required in required_right.items():
        if required not in compact_right:
            failures.append(f"R overlay: {label} が見つからない")
    if "CONFIG_PMW3610_INVERT_X=y" not in right_conf:
        failures.append("R conf: COROPIT実機方向のX反転が設定されていない")
    if "CONFIG_PMW3610_INVERT_Y=y" in right_conf:
        failures.append("R conf: 実機確認済みのY方向を反転している")

    transform_match = re.search(
        r"default_transform:.*?\bmap\s*=\s*<(.*?)>;", dtsi, re.S
    )
    actual_transform = (
        [(int(row), int(col)) for row, col in re.findall(r"RC\((\d+),(\d+)\)", transform_match.group(1))]
        if transform_match else []
    )
    expected = [
        (row, col)
        for row in range(4)
        for col in range(10)
        if (row, col) not in ((3, 0), (3, 8))
    ]
    if actual_transform != expected:
        failures.append(f"matrix transform: {actual_transform} != {expected}")
    editor_transform = [(key["row"], key["col"]) for key in layout]
    if editor_transform != expected:
        failures.append(f"info.json matrix order: {editor_transform} != {expected}")

    key_count = len(expected)
    failures.extend(physical_layout_failures(dtsi, layout))

    try:
        keymap_block = node_block(keymap, "keymap")
    except ValueError as exc:
        failures.append(str(exc))
        keymap_block = ""
    for layer in LAYERS:
        match = re.search(
            rf"\b{re.escape(layer)}\s*\{{\s*bindings\s*=\s*<(.*?)>;\s*\}};",
            keymap_block,
            re.S,
        )
        binding_count = len(re.findall(r"&[A-Za-z_][A-Za-z0-9_]*", match.group(1))) if match else 0
        if binding_count != key_count:
            failures.append(f"{layer}: binding数 {binding_count} != {key_count}")

    # レイヤー番号は宣言順で決まる。LAYERSを番号の正とするため順序も見る。
    declared = re.findall(r"\b(\w+_layer)\s*\{\s*bindings\s*=", keymap_block)
    if declared != list(LAYERS):
        failures.append(f"keymapのレイヤー宣言順: {declared} != {list(LAYERS)}")

    # ComboはKeymap Editorで扱える標準zmk,combosに一本化する。
    # DYA runtime comboとの二重管理は復旧経路を壊しやすいため禁止する。
    try:
        combos = node_block(keymap, "combos")
    except ValueError as exc:
        failures.append(str(exc))
        combos = ""

    if 'compatible = "zmk,combos"' not in combos:
        failures.append("keymap: 標準zmk,combosのcompatibleが見つからない")
    if "runtime_combo_defaults" in keymap or 'cormoran,runtime-combo-defaults' in keymap:
        failures.append("keymap: runtime combo defaultsを使わず標準zmk,combosに一本化する")

    expected_combos = {
        "settings_layer": ([33, 34], "&mo 4"),
        "left_click": ([16, 17], "&mkp LCLK"),
        "right_click": ([17, 18], "&mkp RCLK"),
    }
    for name, (positions, binding) in expected_combos.items():
        try:
            combo = node_block(combos, name)
        except ValueError as exc:
            failures.append(str(exc))
            continue
        position_match = re.search(r"key-positions\s*=\s*<([^>]*)>", combo)
        binding_match = re.search(r"bindings\s*=\s*<([^>]*)>", combo)
        actual_positions = (
            [int(value) for value in re.findall(r"\d+", position_match.group(1))]
            if position_match else []
        )
        actual_binding = (
            re.sub(r"\s+", " ", binding_match.group(1)).strip()
            if binding_match else ""
        )
        if (actual_positions, actual_binding) != (positions, binding):
            failures.append(
                f"ZMK combo {name}: "
                f"{(actual_positions, actual_binding)} != {(positions, binding)}"
            )

    settings_combo = node_block(combos, "settings_layer") if combos else ""
    if not re.search(r"timeout-ms\s*=\s*<80>", settings_combo):
        failures.append("ZMK combo settings_layer: timeout-ms=80が見つからない")

    # トラックボールのスクロール先レイヤーが、keymapのscroll_layerと同じ番号か。
    scroller = re.search(r"scroller\s*\{(.*?)\}", right, re.S)
    scroll_layers = (
        [int(value) for value in re.findall(r"\d+", re.search(r"layers\s*=\s*<([^>]*)>", scroller.group(1)).group(1))]
        if scroller and re.search(r"layers\s*=\s*<([^>]*)>", scroller.group(1))
        else []
    )
    if scroll_layers != [LAYERS.index("scroll_layer")]:
        failures.append(
            f"R overlay scroller: layers={scroll_layers} != "
            f"[{LAYERS.index('scroll_layer')}] (keymapのscroll_layer)"
        )

    # ズーム用のゲイン切替先も、keymapのzoom_layerと同じ番号か。
    zoomer = re.search(r"zoomer\s*\{(.*?)\}", right, re.S)
    zoom_layers = (
        [int(value) for value in re.findall(r"\d+", re.search(r"layers\s*=\s*<([^>]*)>", zoomer.group(1)).group(1))]
        if zoomer and re.search(r"layers\s*=\s*<([^>]*)>", zoomer.group(1))
        else []
    )
    if zoom_layers != [LAYERS.index("zoom_layer")]:
        failures.append(
            f"R overlay zoomer: layers={zoom_layers} != "
            f"[{LAYERS.index('zoom_layer')}] (keymapのzoom_layer)"
        )

    # listenerの子ノードは宣言順に評価され、最初に一致したoverrideで打ち切られる
    # (ZMK input_listener.cのfilter_with_input_config)。zoom_layerはscroll_layerを
    # 保持したまま入るため、zoomerが後ろにあるとズーム中も通常のゲインになる。
    if zoomer and scroller and right.index("zoomer") > right.index("scroller"):
        failures.append("R overlay: zoomerはscrollerより前に宣言する(先勝ちのため)")

    # オートマウスレイヤーは誤爆を避けるため使わない方針(参照だけを見る)。
    if "&zip_temp_layer" in dtsi or "&zip_temp_layer" in right:
        failures.append("trackball_listener: zip_temp_layer(オートマウス)は使わない方針")

    build = text(ROOT / "build.yaml")
    for required in ("shield: c4mtb_r", "shield: c4mtb_l", "shield: settings_reset"):
        if required not in build:
            failures.append(f"build.yaml: {required} が見つからない")

    west = text(ROOT / "config" / "west-dependency.yml")
    required_dependencies = (
        "revision: main+dya",
        "revision: v4.1.0+zmk-fixes+nrf-half-duplex-uart",
        "name: zmk-driver-pmw3610-with-custom-studio-rpc",
        "name: zmk-module-runtime-input-processor",
        "name: zmk-scroll-snap",
    )
    for dependency in required_dependencies:
        if dependency not in west:
            failures.append(f"west-dependency.yml: {dependency} が見つからない")

    dya_first_capacity = (
        "CONFIG_ZMK_STUDIO_TRANSPORT_BLE=y",
        "CONFIG_ZMK_STUDIO_LOCK_BLE_DIRECT_ADVERTISING_ON_UNLOCK=y",
        "CONFIG_ZMK_RUNTIME_MACRO_COUNT=16",
        "CONFIG_ZMK_RUNTIME_MACRO_POOL_BYTES=2048",
    )
    for setting in dya_first_capacity:
        if setting not in dya_conf:
            failures.append(f"DYA Studio: {setting} が見つからない")
    if "CONFIG_ZMK_RUNTIME_COMBO" in dya_conf:
        failures.append("DYA Studio: runtime comboを有効化せず標準zmk,combosを使う")

    if "board: xiao_ble//zmk" not in build:
        failures.append("build.yaml: Zephyr 4.1のxiao_ble//zmk boardが見つからない")
    for processor in ("mouse_runtime_input_processor", "c4mtb_scroll_runtime", "c4mtb_zoom_runtime"):
        if processor not in right:
            failures.append(f"R overlay: DYA runtime processor {processor} が見つからない")

    if failures:
        print("FAIL: firmware/hardware mismatch")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("PASS: firmware GPIO / matrix / physical layout / keymap (38 keys)")
    print("  R: central + PMW3610, L: peripheral, matrix=4x10 (2 unused)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
