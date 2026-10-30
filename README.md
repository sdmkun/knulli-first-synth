# Knulli First Synth

TrimUI Brick（Knulli OS）向けの最小構成ポリフォニックシンセです。GrainBrick のような「Brick で動く楽器アプリ」を作るための土台として作りました。

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
                 クオンタイズ録音、オーバーダブ 1 周ぶんのアンドゥ、メトロノーム、マスターのソフトクリップ
src/drumkit.*    WAV の読み込み（8/16/24/32bit PCM、32bit float、ステレオはモノラルに）と、8 パッドのワンショットサンプラー
src/instrument.h シンセとドラムキットの共通インターフェース
src/main.cpp     画面描画、入力（キーボード / GameController / 生ジョイスティック）、オーディオ出力
src/font.h       5x7 ビットマップフォント
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

| Brick | PC キーボード | 動作 |
|---|---|---|
| 十字キー | 矢印キー / WASD | 音階の 1〜4 音目（EDIT モードではパラメータの選択と変更） |
| A B X Y | K L J I | 音階の 5〜8 音目 |
| L1 / R1 | Q / E | 選んでいるトラックのプリセット（ドラムのトラックではキット）の切替 |
| L2 / R2 | Z / C | オクターブ − / ＋ |
| START | Enter | シーケンサーの再生 / 停止（停止すると頭に戻る） |
| R3（正面中央上の右） | R | 録音のオン / オフ。止まっているときは再生も始める |
| L3（正面中央上の左、単押し） | T | 次のトラックへ（離したときに切り替わる） |
| SELECT（単押し） | Tab | PLAY / EDIT モードの切替（離したときに切り替わる） |
| SELECT + START | Esc | 終了 |

### L3 を押しながら（押している間はパッドの表示が切り替わります）

| Brick | PC キーボード | 動作 |
|---|---|---|
| L3 + 十字キー（左 / 下 / 右 / 上） | T + 矢印 | トラック 1 / 2 / 3 / 4 を選ぶ |
| L3 + X / A / B / Y | T + J / K / L / I | トラック 1 / 2 / 3 / 4 のミュート |
| L3 + R1 | T + E | 選んでいるトラックをソロ（もう一度で解除） |
| L3 + L1 | T + Q | 選んでいるトラックをシンセ ⇄ ドラムに切り替える |

### SELECT を押しながら（押している間は画面下に一覧が出ます）

| Brick | PC キーボード | 動作 |
|---|---|---|
| SELECT + A | Tab + K | 録音のオン / オフ（R3 と同じ） |
| SELECT + B | Tab + L | 選んでいるトラックのアンドゥ（直前のオーバーダブ 1 周ぶん、またはクリアを取り消す） |
| SELECT + X | Tab + J | 選んでいるトラックのパターンを消す |
| SELECT + Y | Tab + I | メトロノーム（CLK）のオン / オフ。録音中だけ鳴る |
| SELECT + 左右 | Tab + 矢印 | BPM −1 / ＋1（押しっぱなしで連続、40〜240） |
| SELECT + 上下 | Tab + 矢印 | 選んでいるトラックのパターンの長さ ＋1 / −1（1〜64 ステップ）。トラックごとに違う長さにするとポリメーターになる |
| SELECT + L1 / R1 | Tab + Q / E | スケールの切替（MAJOR / MINOR / DORIAN / PENTA / BLUES） |
| SELECT + L2 / R2 | Tab + Z / C | キー（ルート音）の切替 |

### シーケンサー

- トラックは 4 つ。それぞれ自分のシンセ（プリセット）とパターンを持ちます。パッド、プリセット、EDIT、録音は選んでいるトラックに効きます
- 1 ステップ = 16 分音符。右上のパネルが 4 トラックのパターンで、音が入っているステップが水色、再生位置がオレンジ（録音中の選択トラックは赤）です。画面下の帯は全体の波形です
- ミュートしたトラックはシーケンサーの音を鳴らしません（パッドで弾いた音は鳴ります）
- 録音中にパッドを弾くと、いちばん近いステップに置かれます（クオンタイズ）。押していた長さもステップ単位で記録します
- 次のステップに置かれた音は、すでに手で鳴らしているので、その周だけはシーケンサーからは鳴らしません（二重に鳴らないように）
- 録音位置は出力バッファ 1 つぶんの遅れを差し引いて決めています
- 1 ステップに置ける音は 4 つまで。同じ音程を同じステップに弾いたときは上書きします
- 音の出ていないトラック（ボイスなし、ディレイの残りもなし）は計算を省きます
- DSP の軽量化：サイン波と LFO は波形テーブル、リミッターは tanh の近似式、位相の折り返しは比較だけ。DLY MIX が 0 のときはディレイの計算を省きます

### パラメータ（EDIT モードで十字キー上下に選び、左右で変更。一覧はスクロールします）

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

- トラック 4 は最初からドラム（キット BASIC）です。どのトラックも L3 + L1 でシンセとドラムを切り替えられます
- 8 つのパッドにキットの 8 音が並びます（十字キー左・下・右・上、X・A・B・Y の順）。スケールとオクターブは関係ありません
- 鳴らした音は最後まで（または END まで）鳴ります。録音もシンセと同じです
- EDIT では、最後に叩いたパッドの設定を変えます（枠がオレンジのパッド）

| 名前 | 内容 |
|---|---|
| PAD | 設定するパッド |
| PITCH | 再生ピッチ（±24 半音） |
| START / END | 再生する範囲（サンプルの何 % から何 % まで） |
| VOLUME | 音量 |

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
| 正面中央上の 2 ボタン | LEFTSTICK / RIGHTSTICK | L3（トラック） / R3（録音） |

### ボタンが反応しない・割り当てがずれるとき

画面最下部の `IN:` 行に、最後に受け取った入力がそのまま表示されます。

- `PAD BTN ...`：SDL GameController として認識されている
- `RAW JOY BUTTON n`：マッピングがないため、生の番号で受け取っている。`src/main.cpp` の `kRawJoyButtons` を実機の番号に合わせて直してください。もしくは、アプリと同じフォルダに `gamecontrollerdb.txt` を置けば読み込まれます。

## 次にやること

- スウィング
- プロジェクトの保存 / 読み込み（JSON）
- MIDI クロック受信
- リバーブ
- サンプル録音とグラニュラーエンジン（GrainBrick 方向）
- プリセットの保存
