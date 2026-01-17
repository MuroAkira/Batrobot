# Batrobot — サーモホン制御／ADC計測ツール

Thermophone（サーモホン）を **C言語＆Makefile で制御・計測するプログラム群** です。  
Raspberry Pi など Linux 環境で動かし、ADC から生データを取得して音データとして確認することを主目的としています。:contentReference[oaicite:1]{index=1}

---

## 🎯 目的（What）

1. サーモホンに狙ったパルス列を出力する  
2. マイク（ADC）で受信する  
3. ADC 生データを確実に取得する  
4. 得られたデータを波形・スペクトル・WAV で確認する  
（これらすべてを安全に、段階的に実装します）:contentReference[oaicite:2]{index=2}

---

## ⚠️ 注意（Safety）

- **Level2 以降は音が出る可能性があります。**  
  必ず手動で確認・了承を取ってから進めてください。:contentReference[oaicite:3]{index=3}
- 実機接続には権限（`dialout` 等のシリアルアクセス）が必要です。

---

## 🗂 リポジトリ構成

```text
.
├─ C/
│  ├─ Makefile
│  ├─ README.md
│  ├─ include/
│  │   ├─ config.h
│  │   ├─ ctrl_port.h
│  │   ├─ pulse_port.h
│  │   ├─ adc_port.h
│  │   └─ timing.h
│  ├─ src/
│  │   ├─ main.c
│  │   ├─ ctrl_port.c
│  │   ├─ pulse_port.c
│  │   ├─ adc_port.c
│  │   └─ timing.c
│  └─ build/
├─ tools/
│  └─ (WAV conversion, helpers)
└─ docs/
   └─ (詳細設計・手順)

