#!/usr/bin/env python3
import argparse
import numpy as np

def read_lr_from_bin(path: str):
    """ADC bin: [LH,LL,RH,RL] big-endian per frame -> int16 L,R"""
    raw = np.fromfile(path, dtype=np.uint8)
    raw = raw[: (raw.size // 4) * 4].reshape(-1, 4)

    l_u16 = (raw[:, 0].astype(np.uint16) << 8) | raw[:, 1].astype(np.uint16)
    r_u16 = (raw[:, 2].astype(np.uint16) << 8) | raw[:, 3].astype(np.uint16)

    L = l_u16.view(np.int16).astype(np.float32)
    R = r_u16.view(np.int16).astype(np.float32)
    return L, R

def moving_rms(x: np.ndarray, win: int) -> np.ndarray:
    x2 = x * x
    kernel = np.ones(win, dtype=np.float32) / float(win)
    return np.sqrt(np.convolve(x2, kernel, mode="same"))

def find_event(env: np.ndarray, fs: int, noise_ms: float, k: float, hold_ms: float):
    """
    env: RMS envelope
    noise_ms: initial noise window length
    k: threshold multiplier
    hold_ms: require 'above threshold' to hold this long to avoid false trigger
    """
    n0 = max(1, int(noise_ms * 1e-3 * fs))
    noise_floor = float(np.median(env[:n0])) + 1e-9
    thr = noise_floor * k

    above = env > thr
    hold = max(1, int(hold_ms * 1e-3 * fs))

    # Find first index where above stays True for 'hold' samples
    idx = None
    run = 0
    for i, a in enumerate(above):
        if a:
            run += 1
            if run >= hold:
                idx = i - hold + 1
                break
        else:
            run = 0

    if idx is None:
        return None, None, noise_floor, thr

    # Find end: last index after start where it remains above, then drops and stays low a bit
    # Simple: scan forward until we've seen 'hold' consecutive False after being True.
    end = None
    run_low = 0
    started = False
    for i in range(idx, len(above)):
        if above[i]:
            started = True
            run_low = 0
        else:
            if started:
                run_low += 1
                if run_low >= hold:
                    end = i - hold + 1
                    break
    if end is None:
        end = len(above) - 1

    return idx, end, noise_floor, thr

def report(name, s, e, fs):
    if s is None:
        print(f"{name}: not found")
        return
    start_us = s / fs * 1e6
    end_us   = e / fs * 1e6
    dur_ms   = (e - s) / fs * 1e3
    print(f"{name}: start={s} ({start_us:.1f} us), end={e} ({end_us:.1f} us), dur={dur_ms:.3f} ms")

def main():
    ap = argparse.ArgumentParser(description="Detect pulse onset inside ADC bin using RMS envelope.")
    ap.add_argument("-i", "--input", default="output/adc_data/adc_dump.bin")
    ap.add_argument("--fs", type=int, default=1_000_000, help="sample rate (Hz)")
    ap.add_argument("--win-us", type=float, default=200.0, help="RMS window (microseconds)")
    ap.add_argument("--noise-ms", type=float, default=5.0, help="noise estimate window from start (ms)")
    ap.add_argument("--k", type=float, default=6.0, help="threshold multiplier over noise floor")
    ap.add_argument("--hold-us", type=float, default=200.0, help="require above-threshold hold (microseconds)")
    args = ap.parse_args()

    L, R = read_lr_from_bin(args.input)
    fs = args.fs

    win = max(3, int(args.win_us * 1e-6 * fs))
    hold_ms = args.hold_us / 1000.0  # us -> ms

    envL = moving_rms(L, win)
    envR = moving_rms(R, win)

    sL, eL, nL, tL = find_event(envL, fs, args.noise_ms, args.k, hold_ms)
    sR, eR, nR, tR = find_event(envR, fs, args.noise_ms, args.k, hold_ms)

    print(f"file: {args.input}")
    print(f"frames: {len(L)}  duration: {len(L)/fs*1000:.3f} ms  fs={fs} Hz")
    print(f"RMS window: {win} samples ({win/fs*1e6:.1f} us)")
    print(f"L: noise_floor={nL:.3f}  thr={tL:.3f}  k={args.k}")
    report("L", sL, eL, fs)
    print(f"R: noise_floor={nR:.3f}  thr={tR:.3f}  k={args.k}")
    report("R", sR, eR, fs)

    # 同期ズレの目安（左右の開始差）
    if sL is not None and sR is not None:
        d_us = (sR - sL) / fs * 1e6
        print(f"start diff (R-L): {d_us:.1f} us")

if __name__ == "__main__":
    main()
