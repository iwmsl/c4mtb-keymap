# C4MTB キーマップとファームウェア

C4MTB（XIAO nRF52840、右手 COROPIT）のキー配列を編集し、書き込み用の UF2 をビルドできます。

## キー配列を変更する

1. [customkeymap](https://customkeymap.com/) の GitHub 入力欄に `iwmsl/c4mtb-keymap` を入力し、**Load** を押す。
2. キーを編集し、**Commit to GitHub** で保存する。ファイルをダウンロードした場合は、GitHub の `config/c4mtb.keymap` にその内容を反映する。
3. [Actions](https://github.com/iwmsl/c4mtb-keymap/actions/workflows/build.yml) で保存したコミットの **Build ZMK firmware** を開き、完了後に **zmk-firmware** をダウンロードする。
4. ZIP を展開し、右手側に `c4mtb_R.uf2` を書き込む。左手側のファームウェアを更新する場合は `c4mtb_L.uf2` を使用する。

## カーソル速度を切り替える

左右中央の拡張キー（初期配列の `Tab` と `左Alt` の位置）を同時に押したまま、`U` の位置を押す。標準の1.25倍と高速の1.50倍が交互に切り替わる。電源を切る場合は、設定が保存されるまで切り替えてから10秒以上待つ。

## ファイル構成

- `config/c4mtb.keymap` にキー割り当て、レイヤー、コンボを定義する。
- `config/info.json` は customkeymap 用の38キーの物理レイアウト。
- `boards/shields/c4mtb/` に左右のピンとトラックボールの設定を定義する。
- `build.yaml` に左右の UF2 とメンテナンス用ビルドを定義する。

メンテナンス時に限り、設定の消去には `settings_reset.uf2`、使用統計の読出しには `c4mtb_R_telemetry_export.uf2` を使用する。[使用統計の仕様](firmware/telemetry.md)

## License

GPL-3.0. See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).
