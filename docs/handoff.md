# 引き継ぎメモ（実装状況と今後の予定）

最終更新: 2026-11-02（一覧ポップアップ・グルーヴ・サンプルのファイルブラウザを足した時点）

次のセッションはこのファイルと `README.md`（操作・構成の説明）、`docs/requirements.md`（要件）を読めば再開できるように書いています。

## 1. いまの状態のひとこと

TrimUI Brick（Knulli）向けの 4 トラックのグルーブボックス。M8 風の画面遷移で、打ち込みはトラッカー式の手入力と生成（ジェネラティブ）の組み合わせ。

- PC（Linux、xvfb）ではビルドと画面の操作、音の出力を確認済み
- **実機での確認（2026-11-02 そまく）**: EVOLVE / MIXER / スウィングまでの版で DSP 20% 以下、ピーク 30% 以下
- **実機で未確認のもの**: 一覧ポップアップ、グルーヴ、ファイルブラウザ
- 実機で確認済みなのは、旧 UI（十字型のパッド配置）での 4 トラック録音・再生、WAV ドラム、DSP 負荷（下の 5 章）まで

## 2. コミットの流れ

| commit | 内容 |
|---|---|
| 2989c34 | 最初のシンセ（フィルターエンベロープ、LFO、ディレイ、プリセット 9 種） |
| 34b1188 | ステップシーケンサー（クオンタイズ録音） |
| a0b7dba | 4 トラック化 |
| e035eb6 | DSP の軽量化 |
| e60347b | WAV ドラムキット |
| d856cf3 | 1 ステップ 8 音まで |
| dea8db9 | M8 風の 3 画面（SEQ / PATTERN / INSTR）と生成（generate.cpp）。パッド演奏と録音は外した |
| 8286940 | LEN をやめて「次のノートか OFF まで鳴る」方式に。ベロシティ 8 段階 |
| 22af136 | 1 ステップ 12 ティック、ノートコマンド（RET / ARP / KIL / DLY / CHA）、GENERATE をメニュー化、ドラムは B + A で消す |
| da2869a | シンセ強化（ウェーブテーブル 7 種、POS、SUB、NOISE、DRIVE、フィルター 4 種、ADSR、ENV 2、LFO × 2）と MODS 画面 |
| eff698f | EVOLVE（N 周ごとに自動ミューテート、VARY / DRIFT）、MIXER 画面（音量とレベルメーター、SEQ の下）、スウィング（SEQ の SWING 欄） |
| 340d612 | 一覧ポップアップ（FX コマンドを A + 上下で選ぶ）、グルーヴ（.agr の読み込みと選択、MIXER で TIMING / VELOCITY）、サンプルのファイルブラウザ（INSTR のパッドの SAMPLE） |
| (この次のコミット) | シンセの FX を行単位（右端の FX1 / FX2、ノートのない行でも鳴っている音に効く）に、空の FX で A = 直前の FX、Y + 十字キーで画面遷移（Y のミューテートは廃止、GENERATE メニューに残す） |

## 3. コードの地図

| ファイル | 役割 |
|---|---|
| `src/main.cpp` | SDL の初期化、オーディオコールバック、入力の割り当て（キーボード / GameController / 生ジョイスティック）、ボタンの振り分け、メインループ、起動時のデモ曲 |
| `src/app.h` / `app.cpp` | 画面どうしで共有する状態 `App`（トラック、カーソル、アンドゥ、生成の設定、GENERATE メニューの状態）と操作。シンセのパラメータは `kDesc` の表で一括管理（名前・種類・メンバーポインタ）。`ParamId` の前半 `kInstrParams` 個が INSTR、残りが MODS |
| `src/ui.cpp` | 上の帯（画面マップ、トラック名、DSP）、下の帯（再生状態、ミュート、スコープ）、`drawField` / `drawParam` |
| `src/ui_seq.cpp` | SEQ：4 列のトラッカー一覧と BPM / KEY / SCALE |
| `src/ui_pattern.cpp` | PATTERN：シンセは N / V / FX コマンド / FX 値 × 4 列、ドラムは 8 × 16 のグリッドと右側のマス詳細 |
| `src/ui_gen.cpp` | GENERATE メニュー（X で開く） |
| `src/ui_instr.cpp` | INSTR：シンセのパラメータと波形表示、ドラムのキットとパッド設定 |
| `src/ui_mods.cpp` | MODS：AMP ENV / ENV 2 / LFO 1 / LFO 2 の 4 枠 |
| `src/ui_popup.cpp` | 一覧のポップアップ（`App::popup`）。ノートコマンド、グルーヴ、サンプルのファイルブラウザの 3 つの一覧を作る関数もここ |
| `src/groove.*` | .agr の読み込み。XML 版はタグを文字列検索、バイナリ版（先頭 AB 1E）は "MidiNoteEvent" の後ろの 25 バイトのレコード（time double / duration double / velocity float / off velocity float / enabled byte）と、末尾近くの FF 00 00 00 + base + 4 つの float（quantize / timing / random / velocity）を読む |
| `src/ui_mixer.cpp` | MIXER：4 トラックの音量フェーダーとレベルメーター、EVOLVE の設定（EVOLVE / AMOUNT / MODE） |
| `src/sequencer.*` | オーディオスレッドで動くシーケンサー。ティック単位で発音、列ごとの状態（鳴っているキー、DLY 待ち、RET、ARP、KIL）。マスターのソフトクリップ |
| `src/generate.*` | ドラム生成（スタイル表、ユークリッド、ゴースト、フィル、ロール）、メロディ生成（スケール内のランダムウォーク、OFF で長さ）、ミューテート |
| `src/synth.*` | シンセ本体とプリセット 15 種 |
| `src/wavetable.*` | ウェーブテーブル 7 種。FFT で倍音を切った 9 段階を起動時に作る（約 10ms） |
| `src/drumkit.*` | WAV 読み込みと 8 パッドのサンプラー（`kill` で短いフェードで切る） |
| `src/instrument.h` | シンセとドラムの共通インターフェース（noteOn / noteOff / setPitch / kill / render） |
| `src/gfx.*`、`font.h` | 描画の道具と 5x7 フォント（小文字・`^`・`|` はない） |

### 設計上の決めごと

- オーディオスレッドと共有するものは必ず `AudioLock`（SDL のオーディオデバイスロック、再帰可）の中で触る
- `ScreenOps`（draw / move / edit / enter / clear / option / hints）で画面を差し替える。ボタンの意味は全画面で共通：A = 置く・鳴らす・開く、A + 十字 = 値を変える（左右 = 細かく、上下 = 大きく）、B + A = 消す・初期値、B + 十字 = 副の値（いまはドラムのノートコマンドだけ）
- ノートは「同じ列の次のノートか OFF まで鳴る」（パターンの終わりで頭に回り込む）。シーケンサーのキーは列ごとに 100 番台を回して使う（前の音の余韻が次の音と重なるように）
- ベロシティは 1〜8、6 が等倍。`velocityGain()` の表で音量に変換
- ドラムの音程は MIDI 36 + パッド番号。生成はパッド順 KICK / SNARE / HAT / OPENHAT / CLAP / TOM / RIM / BELL を前提にしている
- 生成は同じ設定・同じシードなら同じ結果（xorshift）。アンドゥはトラックごとに 1 段で、押すたびに入れ替わる（リドゥを兼ねる）
- EVOLVE はオーディオスレッドで動く：`Sequencer::onLoop`（パターンが頭に戻ったとき、そのステップを鳴らす前に呼ぶ）から `evolveOnLoop()`。ミューテートは確保なしで軽いのでそのまま呼んでいる。VARY は `Evolve::last`（自分が最後に書いた形）と比べて、違えばユーザーが変えたとみなして base を取り直す
- スウィングはティックの長さで作る（`samplesPerTick()` が直前に鳴らしたティックのステップの偶奇で 2×swing / 2×(1−swing) 倍）。偶奇は曲の頭からの通しのステップで数える
- シンセの FX は `SeqStep::fx[2]`（`StepFx`）。`startStep()` で DLY / CHA を行全体に、RET / ARP / KIL は `trigger()` で各ノートに、空の列で鳴っている音には `startStep()` で直接 `applyFx()`。ドラムは `SeqNote::fx`（マスごと）のまま
- 画面遷移は Y + 十字キー（SELECT + 十字キーも可）。`navigate()`（main.cpp）
- ポップアップが開いている間は、十字キー・A・B・L2/R2 をポップアップが取る（`popupPress`）。コールバックの中でポップアップの中身を書き換えることがあるので、呼ぶときは std::function をコピーしてから呼ぶ。ファイルブラウザは状態を shared_ptr で持ち、項目だけ入れ替える
- グルーヴ：ステップごとの開始位置（ステップ単位）とゲインの表を `Sequencer::setGroove` で渡す。`samplesPerTick()` がそのステップの長さを使う（スウィングより優先）。ゲインは `trigger()` で掛ける
- パッドのサンプルを差し替えると、そのトラック用の `App::ownKit` にキットをコピーしてから入れ替える（共有のキットは変えない）。差し替えはオーディオロックの中で allOff してから
- トラックの音量 `Sequencer::volume` とレベル `Sequencer::level`（UI が読んで 0 に戻す）は `process()` で掛ける・測る
- エンベロープの減衰は目標値に近づいたら値をそろえる（非正規化数で遅くなるのを防ぐ。これを入れる前は 1 プリセットで処理が 10 倍になった）

## 4. ビルド・テスト・同期の手順

- PC 版（Windows / MinGW）: `scripts\build-windows.ps1` → `build\windows\ksynth.exe`
- Knulli 版（Docker）: `scripts\build-knulli.ps1` → `dist\ports\` の中身を `\\KNULLI\share\roms\ports` へコピー
- どちらも `src/*.cpp` を全部コンパイルする（ファイルを足してもスクリプトは直さなくてよい）
- Linux での確認（Claude のクラウド環境）:
  - `g++ -O2 -std=c++17 -Wall -Wextra src/*.cpp $(sdl2-config --cflags --libs) -o build/linux/ksynth`（警告 0 を保つ）
  - `SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=out.raw KSYNTH_WINDOWED=1` で起動し、`xvfb-run` + `xdotool` でキーを送り、`import -window root` でスクショ
  - PC キーボードの割り当て: 矢印 = 十字、K = A、L = B、J = X、I = Y、Q / E = L1 / R1、Z / C = L2 / R2、Tab = SELECT、Enter = START、T = L3、R = R3、Esc = 終了
  - `xdotool keydown` の直後の 1 打目が落ちることがあるので、修飾キーを押したあとは 0.15 秒ほど待つ
- 同期: Claude のクラウド環境で編集して GitHub（`sdmkun/knulli-first-synth`、main）に push（そまくの OK あり。PR は不要）。PC 側のリポジトリには git bundle を渡して `git fetch` → `git merge --ff-only` でそろえる（PC 側のシェルからは GitHub に届かないため）
- コミットの作者メールは `5726167+sdmkun@users.noreply.github.com`（gmail のままだと push が弾かれる）

## 5. 実機で測った数字（旧 UI 時点）

- 軽量化前: 1 ボイス約 2〜3%
- 軽量化＋ドラム後: シンセ 3 トラックを埋めてピーク 34%。ドラムも埋めて 31〜37 ボイスで DSP 15〜35%、ピーク平均 40%、たまに 80%（たまのピークの原因は未調査、音切れの有無も未確認）
- 新シンセ（da2869a）は x86 の計測で旧シンセと同程度（8 ボイスで実時間の 0.4〜0.7%）。実機は未計測

## 6. そまくの好み・決めたこと

- main に直接 push してよい（2026-11-02）
- FX は右端で行全体にかける。ノートのない行にも置けて鳴っている音に効く。空の FX で A = 直前の FX と値（2026-11-02）
- Y は画面遷移用（M8 の SHIFT のように Y + 十字キー）。Y のミューテートはやめた（2026-11-02）
- ノートコマンドが増えても 1 つずつ送らずに済むよう、M8 のように一覧から選びたい
- グルーヴは Ableton の .agr（そまくの PC の `E:\Downloads\Groove`、259 個）を使う。リポジトリの `grooves/` にコピー済み（git には入れない）

- L / R がオクターブである必要はない。要件の md は細部が適当なところがある
- 遅延はそれほど気にしなくていい。ただし処理落ち（音切れ）はだめ
- 十字キーとボタンで「演奏」したいとは思わない。厳密でなくていいので、プロシージャル / ジェネラティブに打ち込みたい
- 4 トラックはトラッカー風に 4 列で、スクロールしない
- 画面遷移は M8 に寄せる。エンベロープや LFO は別画面（MODS、INSTR から Y + 上）
- ドラムは全パートが見えるグリッドで打ち込む

## 7. 次にやること（候補）

1. **実機で確認**（最優先）: 新しい画面と操作、ノートコマンド、新シンセ、MODS 画面。DSP 負荷とたまのピーク、音切れがないか
2. 実機のフィードバックを直す
3. プロジェクトとプリセットの保存 / 読み込み（JSON）。EVOLVE・音量・スウィング・グルーヴ・差し替えたパッドのサンプルも入れる
4. ノートコマンドの追加（グライド、音量のスライドなど）、メロディ生成で ARP を使う
5. EVOLVE の発展：周ごとに生成し直す（NEW SEED）モード、変える範囲をパートで選ぶ（例：キックは固定）
6. MIDI クロック受信、リバーブ、サンプル録音とグラニュラー（GrainBrick 方向）

## 8. 気になっていること

- たまに DSP ピークが 80% まで上がる原因が未調査
- コードに残っている使っていないもの: なし（旧パッド UI・録音・メトロノームは削除済み）
- ARP の速さは 3 ティック固定。必要なら速さを変えるコマンドを足す
- ウェーブテーブルの段の選び方は控えめ（100Hz で 128 倍音まで）なので、低音はやや暗め。気になるなら `wt::mipFor` を 1 段甘くする
