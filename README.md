# Knulli First Synth

TrimUI Brick（Knulli OS）向けの、4 トラックの小さなグルーブボックスです。打ち込みは生成（ジェネラティブ）とトラッカー式の手入力を組み合わせます。GrainBrick のような「Brick で動く楽器アプリ」を作るための土台として作りました。

## 技術選定

| 候補 | 判断 |
|---|---|
| **C++17 + SDL2** | **採用**。SDL のオーディオコールバックでリアルタイム DSP を回せるので、低レイテンシで処理の重さを気にしなくて済む。Knulli（Batocera 系）は EmulationStation 自体が SDL2 を使っているため、追加ランタイムなしで動く。 |
| Python + pygame-ce | Knulli に標準で入っていて（`/userdata/roms/pygame`）、コンパイルなしで試せる。ただ、サンプル単位の DSP を Python で書くと Cortex-A53 では力不足。numpy が入っている保証もない。 |
| LÖVE (Lua) | PortMaster 経由なら動く。ただし DSP が Lua になり、PortMaster にも依存する。 |
| Rust + SDL2 | 選んでも問題ないが、クロスビルドの手間が増えるわりに利点が小さい。 |

依存するのは SDL2 だけです（文字は内蔵の 5x7 ビットマップフォントで描画）。libstdc++ は静的リンクしています。

## 構成

```
src/synth.*      DSP：8 ボイス、PolyBLEP オシレーター、TPT SVF ローパス、AR エンベロープ、デチューン、
                 フィルターエンベロープ、LFO（PITCH / CUTOFF / AMP）、フィードバックディレイ
src/sequencer.*  4 トラックのステップシーケンサー：オーディオスレッドで駆動、トラックごとのパターン長、ミュート / ソロ、
                 マスターのソフトクリップ
src/generate.*   パターンの生成：ドラムのスタイル表とユークリッドリズム、スケールに沿ったメロディ、ミューテート
src/drumkit.*    WAV の読み込み（8/16/24/32bit PCM、32bit float、ステレオはモノラルに）と、8 パッドのワンショットサンプラー
src/instrument.h シンセとドラムキットの共通インターフェース
src/app.*        画面どうしで共有する状態（トラック、カーソル、アンドゥ、生成の設定）と操作
src/ui*.cpp      画面：ui.cpp（上下の帯）、ui_seq.cpp、ui_pattern.cpp、ui_instr.cpp
src/gfx.*        描画の道具と 5x7 ビットマップフォント（font.h）
src/main.cpp     入力（キーボード / GameController / 生ジョイスティック）、オーディオ出力、メインループ
docker/          aarch64 クロスビルド環境（Debian bookworm、実行には glibc 2.36 以上が必要）
package/         Knulli Ports 用ランチャー
samples/kits/    ドラムキット（BASIC は scripts/make-basic-kit.py で合成したもの）
scripts/         ビルドスクリプト
```

## ビルド

PC で動作確認（Windows / MinGW g++）：

```powershell
.\scripts\build-windows.ps1
.\build\windows\ksynth.exe
```

Knulli 用（Docker Desktop が必要）：

```powershell
.\scripts\build-knulli.ps1
```

`dist\ports\` の中身を、SD カードの `/userdata/roms/ports/`（SMB 経由なら `\\KNULLI\share\roms\ports`）にコピーします。その後 Knulli のメニューで「ゲームリストを更新」を実行すると、Ports に **KnulliFirstSynth** が表示されます。

```
roms/ports/
├── KnulliFirstSynth.sh
├── KnulliFirstSynth.sh.keys
└── knulli-first-synth/
    ├── ksynth
    └── samples/kits/BASIC/*.wav
```

起動ログは `roms/ports/knulli-first-synth/log.txt` に出力されます（SDL のドライバ、オーディオの設定、コントローラー名と GUID）。

## 操作

M8 のように、横に並んだ 3 つの画面を SELECT + 左右で行き来します。画面上端に今いる画面が出ます。

```
SEQ  ⇄  PATTERN  ⇄  INSTR
4 トラック一覧   選んだトラックの打ち込み   選んだトラックの音色
```

基本の操作はどの画面でも同じです。十字キーでカーソルを動かし、A で「置く / 鳴らす / 開く」、A を押したまま十字キーでカーソルの下の値を変えます（左右で細かく、上下で大きく）。B を押したまま A で消す / 元に戻すです。

### どの画面でも効くボタン

| Brick | PC キーボード | 動作 |
|---|---|---|
| 十字キー | 矢印キー / WASD | カーソル移動（押しっぱなしで連続） |
| A | K | 置く / 叩く / 鳴らす / 開く（画面ごとに下を参照） |
| A + 十字キー | K + 矢印 | カーソルの下の値を変える。左右 = 細かく、上下 = 大きく |
| B + A | L + K | カーソルの下を消す / 初期値に戻す |
| X | J | 選んでいるトラックを新しいシードで生成し直す |
| Y | I | 選んでいるトラックをミューテート（少しだけ変える） |
| L1 / R1 | Q / E | トラックの選択 |
| L2 / R2 | Z / C | ページ（パターンが 16 ステップより長いとき） |
| L3（正面中央上の左） | T | 選んでいるトラックのミュート |
| R3（正面中央上の右） | R | 選んでいるトラックのソロ（もう一度で解除） |
| START | Enter | 再生 / 停止（停止すると頭に戻る） |
| SELECT + 左右 | Tab + 矢印 | 画面の切り替え |
| SELECT + B | Tab + L | 選んでいるトラックのアンドゥ（もう一度でリドゥ） |
| SELECT + X | Tab + J | 選んでいるトラックのパターンを消す |
| SELECT + START | Esc | 終了 |

X・Y・クリア・打ち込みの前には、そのトラックのパターンを 1 段だけ控えています。SELECT + B で戻し、もう一度押すとやり直せます。

### SEQ 画面

4 トラックを 4 列に並べたトラッカー風の一覧です。16 ステップぶんを縦に並べ、スクロールはしません（それより長いパターンは L2 / R2 でページを切り替え）。

- シンセのトラックは音名（3 つまで、それより多いと `+`）、ドラムのトラックは 8 パートの頭文字（K S H O C T R B）を出します
- 一番上の行は BPM / KEY / SCALE。カーソルを上に出して A + 十字キーで変えます
- A でそのステップを PATTERN 画面で開きます。B + A でそのステップを消します

### PATTERN 画面

選んでいるトラックの打ち込み画面です。右側の GENERATE パネルが、そのトラックの生成の設定です。パネルの値を A + 十字キーで変えると、その場でパターンが作り直されます（LEN だけはパターンの長さを変えるだけで、中身はそのまま）。

**シンセのトラック**：1 ステップに 4 つの NOTE / LEN の列があるトラッカーです。

- 空いた NOTE で A を押すと、最後に使った音を置きます。A + 左右でスケールの中を 1 音ずつ、A + 上下で 1 オクターブ動きます
- LEN は音の長さ（ステップ数）です
- 生成の設定：OCT（音域の中心）、DENSITY（音の数）、RANGE（音域の広さ）、MOTION（跳躍の多さ）、LENGTH（音の長さ）、CHORD（和音にする割合）、SEED

**ドラムのトラック**：8 パート × 16 ステップのグリッドで、全パートが一度に見えます。

- マスで A を押すと叩く / 消すが切り替わります
- 左端のパート名で A + 左右を押すと、そのパートをユークリッドリズムにします（16 ステップに E 個の音をなるべく均等に並べる）。A + 上下は並びを回転（R）します
- 生成の設定：STYLE（FOUR = 4 つ打ち / BREAK / HALF = ハーフタイム / JUNGLE / RANDOM）、DENSITY、VARIATION（ゴーストノートやフィルの量）、SEED
- ミューテートでは、キックとスネアの骨格（1・3 拍目のキック、2・4 拍目のスネア）は残します

生成は同じ設定・同じシードなら毎回同じ結果になります。メロディは KEY と SCALE に合わせて作ります。

### INSTR 画面

選んでいるトラックの音色です。A で音を確かめられます。

- TYPE：SYNTH ⇄ DRUM の切り替え（A + 十字キー）。切り替えるとパターンは消えます（SELECT + B で戻せます）
- シンセ：PRESET と、下の表のパラメータ。B + A でそのパラメータをプリセットの値に戻します
- ドラム：KIT と、8 パッドぶんの PITCH（±24 半音）/ START / END（再生する範囲）/ VOL。右側に選んでいるパッドの波形と START（緑）・END（赤）が出ます

### シーケンサーのしくみ

- 1 ステップ = 16 分音符。トラックごとにパターンの長さ（1〜64）を変えられ、違う長さにするとポリメーターになります
- 1 ステップに置ける音は 8 つまでです
- ミュートしたトラックは鳴りません。ソロにしたトラックだけが鳴ります
- 音の出ていないトラック（ボイスなし、ディレイの残りもなし）は計算を省きます
- DSP の軽量化：サイン波と LFO は波形テーブル、リミッターは tanh の近似式、位相の折り返しは比較だけ。DLY MIX が 0 のときはディレイの計算を省きます
- 起動直後は、トラック 2 にベースライン、トラック 4 に 4 つ打ちが入っています。START ですぐ鳴ります

### シンセのパラメータ

| 名前 | 内容 |
|---|---|
| WAVE | 波形（SINE / TRIANGLE / SAW / SQUARE / PULSE 25% / NOISE） |
| CUTOFF / RESO | ローパスフィルターのカットオフ周波数とレゾナンス |
| ATTACK / RELEASE | 音量エンベロープ（立ち上がりと余韻） |
| DETUNE | 2 つ目のオシレーターのずらし幅（セント） |
| VOLUME | 音量 |
| FENV AMT / FENV DEC | フィルターエンベロープ。発音時にカットオフを最大 +6 オクターブ開き、DEC の時間で閉じる |
| LFO RATE / LFO DPTH / LFO DEST | LFO の速さ（0.05〜20Hz）、深さ、揺らす先（PITCH ±2 半音 / CUTOFF ±3 オクターブ / AMP） |
| DLY TIME / DLY FDBK / DLY MIX | ディレイの時間（10ms〜1s）、フィードバック、原音に混ぜる量。時間を動かすとテープのようにピッチが揺れる |

### ドラム

- トラック 4 は最初からドラム（キット BASIC）です。どのトラックも INSTR 画面の TYPE でシンセとドラムを切り替えられます
- 8 パートの順番は KICK / SNARE / HAT / OPENHAT / CLAP / TOM / RIM / BELL です（自分のキットでも生成はこの順番を前提にします）
- 鳴らした音は最後まで（または END まで）鳴ります

#### 自分のキットを入れる

`samples/kits/` の下にフォルダを作って WAV を入れると、1 つのキットになります（実機では `roms/ports/knulli-first-synth/samples/kits/`）。

- ファイル名の順に、最初の 8 個がパッド 1〜8 になります。`1-KICK.wav` のように番号を付けると順番を決められ、画面には番号を除いた名前が出ます
- 対応形式：8/16/24/32bit の PCM と 32bit float。ステレオはモノラルにまとめます。サンプルレートは何でも構いません
- キットは起動時に読み込みます。入れ替えたらアプリを起動し直してください

A/B/X/Y の割り当ては SDL GameController の位置名に合わせています（A=下、B=右、X=左、Y=上）。そのため、本体の印字とはずれることがあります。

### 実機での確認結果（TrimUI Brick / Knulli）

Brick の内蔵パッドは、SDL GameController としてそのまま認識されます（生ジョイスティックのフォールバックは使われません）。

| 物理ボタン | SDL での名前 | アプリでの割り当て |
|---|---|---|
| 十字キー | DPUP / DPDOWN / DPLEFT / DPRIGHT | UP / DOWN / LEFT / RIGHT |
| ABXY | A / B / X / Y | A / B / X / Y |
| 背面 L1 / R1 | LEFTSHOULDER / RIGHTSHOULDER | L1 / R1 |
| 背面 L2 / R2 | トリガー軸 | L2 / R2 |
| 正面下部（左から） | GUIDE / BACK / START | MENU / SELECT / START |
| 正面中央上の 2 ボタン | LEFTSTICK / RIGHTSTICK | L3（ミュート） / R3（ソロ） |

### ボタンが反応しない・割り当てがずれるとき

割り当てのないボタンを押すと、画面最下部の行に受け取った入力が表示されます。

- `UNMAPPED PAD BUTTON ...`：SDL GameController として認識されているが、アプリで使っていないボタン
- `RAW JOY BUTTON n`：マッピングがないため、生の番号で受け取っている。`src/main.cpp` の `kRawJoyButtons` を実機の番号に合わせて直してください。もしくは、アプリと同じフォルダに `gamecontrollerdb.txt` を置けば読み込まれます。

## 次にやること

- EVOLVE（数小節ごとに自動でミューテートし続ける）
- MIXER 画面（トラックごとの音量）
- スウィング
- プロジェクトの保存 / 読み込み（JSON）
- MIDI クロック受信
- リバーブ
- サンプル録音とグラニュラーエンジン（GrainBrick 方向）
- プリセットの保存
