#!/usr/bin/env python3
import argparse
import os
import wave
import numpy as np

def read_adc_bin_lrrl_be(path: str):
    """
    Read ADC raw bin with byte order: [LH, LL, RH, RL] per frame.
    Returns int16 arrays: L, R (signed).
    """
    raw = np.fromfile(path, dtype=np.uint8)
    if raw.size % 4 != 0:
        trimmed = raw.size - (raw.size % 4)
        print(f"[warn] input bytes={raw.size} not multiple of 4, trimming to {trimmed}")
        raw = raw[:trimmed]

    frames = raw.reshape(-1, 4)
    # Big-endian 16-bit: (MSB<<8) | LSB
    l_u16 = (frames[:, 0].astype(np.uint16) << 8) | frames[:, 1].astype(np.uint16)
    r_u16 = (frames[:, 2].astype(np.uint16) << 8) | frames[:, 3].astype(np.uint16)

    # Convert to signed int16
    l = l_u16.view(np.int16)
    r = r_u16.view(np.int16)
    return l, r

def write_wav_int16_stereo(path: str, rate: int, l: np.ndarray, r: np.ndarray):
    assert l.dtype == np.int16 and r.dtype == np.int16
    n = min(l.size, r.size)
    interleaved = np.empty(n * 2, dtype=np.int16)
    interleaved[0::2] = l[:n]
    interleaved[1::2] = r[:n]

    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)  # 16-bit
        w.setframerate(rate)
        w.writeframes(interleaved.tobytes())

def resample_linear(x: np.ndarray, in_rate: int, out_rate: int) -> np.ndarray:
    """
    Linear interpolation resampler for int16 mono signal.
    """
    n_in = x.size
    dur = n_in / float(in_rate)
    n_out = int(round(dur * out_rate))
    if n_out <= 1 or n_in <= 1:
        return np.zeros(max(n_out, 0), dtype=np.int16)

    t_in = np.linspace(0.0, dur, num=n_in, endpoint=False)
    t_out = np.linspace(0.0, dur, num=n_out, endpoint=False)

    y = np.interp(t_out, t_in, x.astype(np.float32)).astype(np.float32)
    y = np.clip(np.round(y), -32768, 32767).astype(np.int16)
    return y

def main():
    ap = argparse.ArgumentParser(description="Thermophone ADC bin (LH,LL,RH,RL) -> WAV (1MHz) + downsample WAV (48kHz)")
    ap.add_argument("-i", "--input", default="/home/ubuntu/batrobot/C/output/adc_data/adc_dump_FM_test2_duty40.bin", help="input bin path")
    ap.add_argument("--in-rate", type=int, default=1_000_000, help="input sample rate (Hz), default 1,000,000")
    # ap.add_argument("--out-rate", type=int, default=48_000, help="downsample target (Hz), default 48,000")
    ap.add_argument("-o1", "--out-wav", default="/home/ubuntu/batrobot/C/output/adc_data/adc_dump_FM_test2_duty40.wav", help="output wav (full rate)")

    args = ap.parse_args()

    l, r = read_adc_bin_lrrl_be(args.input)
    print(f"[ok] read: frames={l.size} bytes={l.size*4} duration={l.size/args.in_rate:.6f}s")

    # 1MHz WAV（正しい符号・正しいバイト順で書き出し）
    write_wav_int16_stereo(args.out_wav, args.in_rate, l, r)
    print(f"[ok] wrote: {args.out_wav} (rate={args.in_rate}Hz)")

    # # 48kHz 確認用WAV（線形補間でリサンプル）
    # l48 = resample_linear(l, args.in_rate, args.out_rate)
    # r48 = resample_linear(r, args.in_rate, args.out_rate)
    # write_wav_int16_stereo(args.out_wav48k, args.out_rate, l48, r48)
    # print(f"[ok] wrote: {args.out_wav48k} (rate={args.out_rate}Hz)")

if __name__ == "__main__":
    main()
