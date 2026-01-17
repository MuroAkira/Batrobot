new_bat_robotの説明書

・ファイル構造：
C/
├─ Makefile
├─ README.md
│
├─ include/
│   ├─ config.h
│   ├─ ctrl_port.h
│   ├─ pulse_port.h
│   ├─ adc_port.h
│   └─ timing.h
│
├─ src/
│   ├─ main.c 
│   ├─ ctrl_port.c
│   ├─ pulse_port.c
│   ├─ adc_port.c
│   └─ timing.c
│
└─ build/

ファイルの役割
① main.c
【意味】
司令塔
【責務】
プログラムの流れを決める
初期化の順番を管理
各モジュールを「呼ぶだけ」

② ctrl_port.c / ctrl_port.h
【意味】
制御ポート専用（CP210x）
【担当ポート】
/dev/ttyUSB2
【責務】
・termios でポートを開く
・ENQ → ACK 確認
・f, g, b, t, e コマンド送信
・状態取得（数値を読む）

③ pulse_port.c / pulse_port.h
【意味】
サーモホン駆動用（FT2232H PortA）
【担当ポート】
/dev/ttyUSB0
【責務】
・大量バイト列の write
・送信バッファ管理
・「返ってこない前提」で動く
【重要】
・read() は 絶対に書かない
・write() 専用
→ADCより後に実装

④ adc_port.c / adc_port.h
【意味】
ADCデータ取得専用（FT2232H PortB）
【担当ポート】
/dev/ttyUSB1
【責務】
・read() のみ
・4MB/s を落とさず読む
・バッファリング
・ファイル書き出し用の生データ生成
【重要】
・printf 禁止
・malloc をループ内で使わない
・とにかく「止まらない」ことが使命
→ 一番最後に作る

⑤ timing.c / timing.h
【意味】
測定シーケンス管理
【責務】
・「いつ何をするか」
・パルス送信とADC受信の同期
・t1 / t2 / t3 の関係制御
【ポイント】
・シリアルI/Oは呼ばない
・ロジックだけを書く
→ main とモジュールの橋渡し

⑥ config.h
【意味】
全体の共通設定ファイル
【中身】
・デバイスパス
・バッファサイズ
・タイムアウト
・サンプリング周波数上限
→ マジックナンバーは全部ここへ

・開発の流れ：注意レベルにわけて開発を進める
レベル0：安全（音が出ない）
目的：土台を固める
0-1) Cプロジェクトがビルドできる（ダミーmainでOK）
0-2) Cで制御ポートを open/close できる
0-3) Cで ENQ→ACK が取れる
0-4) Cで f などの文字列コマンド送受信ができる
✅ 完成2026/1/14/19:45

レベル1：データ受信の準備（音はまだ出ない）
目的：ADC側を“読むだけ”で成立させる
1-1) Cで /dev/ttyUSB1 を open/close
1-2) “何も来ない”のを確認（正常）
1-3) 受信バッファクリア（flush）を作る
1-4) 指定バイト数を確実に read してファイルに落とす（生データdump）
✅ 完成2026/1/14/19:51

レベル2：音が出る可能性がある
目的：パルス送信→ADCデータ取得を1回成功させる
2-1) Cで /dev/ttyUSB0 を open
2-2) 最小のパルスデータ（短時間）を送る
2-3) 同時に /dev/ttyUSB1 から指定バイト読む
2-4) 制御ポートで e を読んでエラー0を確認
⚠️ ここから音が出る可能性があります。
👉 必ず事前にあなたに「今からパルス送信します、OK？」と確認します。

レベル3：音として確認（波形/ファイル化）
目的：取れたADCが音になっていると確認
3-1) 生データ（LH LL RH RL）をエンディアン変換
3-2) WAVヘッダつけて保存（仕様書の注意通り）
3-3) 波形を目視 or スペクトルで確認
3-4) 実験条件を変えて再現性を見る（ゲイン/周波数/バイト数など）

ゴール
1.サーモホンから狙った波形（パルス列）を出す
2.マイクで受ける
3.ADCデータとして取得できる
4.取得データが「音として妥当」（波形/スペクトル/ファイル化）で確認できる


・仮想環境での実行と確認
① 今回やったこと
実機（サーモホン基板）に危険な波形（duty 100%など）を出さずに
本番相当のPULSEデータ（duty含む）をC側で生成
送信フォーマットと送受信経路を安全に検証する

実施した内容（整理）
1. 仮想シリアル（偽基板）環境を構築
socat を使って以下の仮想ポートを作成
CTRL：/tmp/CTRL_A ↔ /tmp/CTRL_B
ADC ：/tmp/ADC_A ↔ /tmp/ADC_B
PULSE：/tmp/PULSE_A ↔ /tmp/PULSE_B

実機デバイス（/dev/ttyUSB*）は一切使わない構成
2. PULSE送信を「偽ポート限定」に安全ロック
src/pulse_port.c を改修
pulse_port 構造体に devpath を保持
pulse_write_locked() で以下を強制：
/tmp/PULSE_A のとき のみ送信許可
実機ポートの場合は即エラー
0xFF（8bitすべてHigh）を含むデータは送信拒否
👉 これにより
どんなバグがあっても duty100% 相当の波形が実機に出ない

3. 本番相当のPULSE波形をCで生成
pulse_gen_pfd() を新規実装
入力：周波数（kHz）duty（%）
出力：1bit = High/Low を詰めた生のビット列、1byte = 8パルス
TPS-main.ino の考え方（pfd相当）をCで再現
CRCやヘッダは PortA仕様上存在しないため生成しない

4. main.c を拡張して本番相当フローを通した
main.c で以下を実行：
CTRLポートで ENQ → ACK 確認
PULSE波形を生成して送信（偽PULSEのみ）
ADCポートからデータを読み込み
👉 実機なしで 本番と同じ処理順 を最後まで通過可能

5. 偽PULSEで送信内容を可視化
/tmp/PULSE_B 側で Python ロガーを起動
送信された 生バイト列（HEX）を直接確認
duty を変えると 対応するビットパターンが変化することを確認

実行手順
1) 仮想ポートを起動（3つ・起動したまま）

・CTRLのsocat
rm -f /tmp/CTRL_A /tmp/CTRL_B
socat -d -d pty,raw,echo=0,perm=660,link=/tmp/CTRL_A pty,raw,echo=0,perm=660,link=/tmp/CTRL_B

・ADCのsocat
rm -f /tmp/ADC_A /tmp/ADC_B
socat -d -d pty,raw,echo=0,perm=660,link=/tmp/ADC_A pty,raw,echo=0,perm=660,link=/tmp/ADC_B

・PULSEのsocat
rm -f /tmp/PULSE_A /tmp/PULSE_B
socat -d -d pty,raw,echo=0,perm=660,link=/tmp/PULSE_A pty,raw,echo=0,perm=660,link=/tmp/PULSE_B

2) 偽デバイスを起動
・CTRL_B（ENQ→ACK）で実行するコマンド
python3 - <<'PY'
import os, time
fd=os.open("/tmp/CTRL_B", os.O_RDWR|os.O_NOCTTY)
while True:
    b=os.read(fd, 64)
    if not b: time.sleep(0.01); continue
    if b"\x05" in b: os.write(fd, b"\x06")
PY

・ADC_B（ダミー送信）で実行するコマンド
python3 - <<'PY'
import os, time, struct
fd=os.open("/tmp/ADC_B", os.O_RDWR|os.O_NOCTTY)
x=0
while True:
    os.write(fd, struct.pack("<H", x & 0xFFFF))
    x+=1; time.sleep(0.01)
PY

・PULSE_B（ログ）で実行するコマンド
python3 - <<'PY'
import os, time, binascii
fd=os.open("/tmp/PULSE_B", os.O_RDWR|os.O_NOCTTY)
while True:
    b=os.read(fd, 4096)
    if not b: time.sleep(0.01); continue
    print("PULSE HEX:", binascii.hexlify(b).decode())
PY

3) ビルド＆実行
cd ~/batrobot/C
make clean
make
./build/thermophone

・期待される結果
端末：
ENQ/ACK OK
pulse_write OK bytes=...
adc_read bytes=...

PULSE_B 側に PULSE HEX: ... が表示される
→ 本番相当のPULSEビット列が生成・送信できています（/tmp限定・安全）

・20226/1/17 今日できたこと（成果）
Raspberry Pi（Ubuntu 24.04）上で、Thermophone基板と3ポート通信が動作

PortA /dev/ttyUSB0：パルス送信（音が出ること確認）

PortB /dev/ttyUSB1：ADC受信（64ms = 256000 byte を安定取得）

PortC /dev/ttyUSB2：コマンド（ENQ/ACK、ゲイン設定）

パルス生成 → 送信 → ADC録音 → bin保存が main で1回の実行で完結

ADCのbinを 仕様通りのバイト順（LH,LL,RH,RL）から正しく16bit化し、
1MHz WAV化 + 48kHzへダウンサンプルして「音が入っている」ことを確認

stdout のバッファでログが欠ける問題を stdbuf で回避し、ログ取得を安定化

マイクゲイン g 300 を PortC から設定して録音レベルを改善

2. 重要な仕様・前提（READMEに残すべきポイント）

ADC設定：fs = 1MHz, データレートは 4 byte / sample（LH,LL,RH,RL）

64ms録音の固定値：
1,000,000 × 0.064 × 4 = 256,000 byte

パルス長 pt は 送信バイト数 pb で決まる（10MHz基準：1bit=0.1µs、1byte=0.8µs）

10ms = 12,500 byte

40ms = 50,000 byte

ログは printf がバッファされるので、基本は stdbuf付きで実行

PortAは pulse_port.c の安全ロックで /dev/ttyUSB0のみ許可（実機誤送信防止）

3. 変更した/追加したファイル（想定）

最低限これを Git に入れるのがおすすめです。

変更

src/main.c

g 300 を送る

ADCスレッド開始 → パルス送信 → ADC待ち → adc_dump.bin 保存

パルス設定（例：pb=12500 / 40kHz / duty=10）

src/ctrl_port.c

struct ctrl_port に devpath を追加（snprintf不整合修正）

ctrl_send_line() を追加（g 300\n 送信用）

include/ctrl_port.h

ctrl_send_line() の宣言追加

追加

tools/adc_bin_to_wav.py

bin（LH,LL,RH,RL）→ 正しく16bit化

1MHz wav出力

48kHz wav（確認用）出力

docs/RUN_RPI.md（新規推奨）

ビルド・実行手順、ポート確認、WAV化まで
4. 実行手順（docs/READMEに貼る用）
cd ~/batrobot/C
make clean
make

# ログを確実に残す（stdoutバッファ対策）
stdbuf -oL -eL ./build/thermophone 2>&1 | tee run.log

# bin → wav（1MHz + 48kHz）
python3 tools/adc_bin_to_wav.py \
  -i output/adc_data/adc_dump.bin \
  --in-rate 1000000 \
  --out-rate 48000 \
  -o1 output/adc_data/adc_dump_1MHz.wav \
  -o2 output/adc_data/adc_dump_48k.wav

aplay output/adc_data/adc_dump_48k.wav

5. 今日の既知の注意点（次回のTODO）

音量が小さい場合：

pt（pb）を戻す（10ms→40ms）または duty を段階的に上げる

g は 録音側の増幅であって、サーモホン出力自体は pb/duty 側

main.c が複製連結されて壊れたことがあったので、Gitに上げる前に

src/main.c が 1本のコードになっているか必ず確認

・FM音を出せた。

