#!/usr/bin/env python3
import argparse
import os
import wave

def bin_to_wav(in_path: str, out_path: str, sample_rate: int = 1_000_000, channels: int = 2, sampwidth: int = 2):
    # sampwidth=2 -> 16-bit
    frame_size = channels * sampwidth  # bytes per sample-frame (L+R)
    with open(in_path, "rb") as f:
        data = f.read()

    if len(data) % frame_size != 0:
        # 端数があるとWAVのフレーム境界が壊れるので切り捨て
        trimmed = len(data) - (len(data) % frame_size)
        print(f"[warn] Input size {len(data)} bytes is not multiple of frame_size={frame_size}. Trimming to {trimmed}.")
        data = data[:trimmed]

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)

    with wave.open(out_path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(sampwidth)
        w.setframerate(sample_rate)
        w.writeframes(data)

    frames = len(data) // frame_size
    duration = frames / sample_rate
    print(f"[ok] Wrote WAV: {out_path}")
    print(f"     channels={channels}, sampwidth={sampwidth*8}bit, sample_rate={sample_rate}Hz")
    print(f"     frames={frames}, duration={duration:.6f} sec, bytes={len(data)}")

def main():
    ap = argparse.ArgumentParser(description="Convert Thermophone ADC bin (LH LL RH RL) to WAV.")
    ap.add_argument("-i", "--input", default="output/adc_data/adc_dump.bin", help="input .bin path")
    ap.add_argument("-o", "--output", default="output/adc_data/adc_dump.wav", help="output .wav path")
    ap.add_argument("--rate", type=int, default=1_000_000, help="sample rate (Hz), default 1,000,000")
    args = ap.parse_args()

    bin_to_wav(args.input, args.output, sample_rate=args.rate)

if __name__ == "__main__":
    main()
