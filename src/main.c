#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "config.h"
#include "ctrl_port.h"
#include "pulse_port.h"
#include "adc_port.h"
#include "crosscorr.h"
#include <math.h>


/* ====== ADC設定（まずはここだけ調整すればOK） ======
   どれだけ読むかは基板側の設定（read_bytes等）と合わせる必要があります */
#ifndef ADC_READ_BYTES
#define ADC_READ_BYTES (256000)     /* 64ms fs 1Mhz */
#endif

#ifndef ADC_TIMEOUT_MS
#define ADC_TIMEOUT_MS (5000)      /* 1000ms */
#endif

#ifndef ADC_SLICE_TIMEOUT_MS
#define ADC_SLICE_TIMEOUT_MS (50)  /* select待ち（小刻みに回す） */
#endif

typedef struct {
    adc_port_t* adc;
    uint8_t* buf;
    size_t want;
    size_t got;
    int ok;             /* 1=成功 */
    int total_timeout_ms;
} adc_thread_ctx_t;

static void gen_ref_exp_chirp_1m(float* refN, int N, double fs,
                                double f0_hz, double f1_hz, double dur_s,
                                double amp)
{
    /* refN は N点。最初の dur_s だけチャープ、残り0 */
    for (int i=0;i<N;i++) refN[i] = 0.0f;

    int M = (int)floor(dur_s * fs + 0.5);
    if (M > N) M = N;
    if (M <= 0) return;

    double r = f1_hz / f0_hz;
    double ln_r = log(r);
    if (ln_r == 0.0) {
        /* f0==f1 のときは単一周波数 */
        for (int n=0;n<M;n++) {
            double t = (double)n / fs;
            refN[n] = (float)(amp * sin(2.0*M_PI*f0_hz*t));
        }
        return;
    }

    /* 位相: phi(t)=2π * f0*T/ln(r) * ( r^(t/T) - 1 ) */
    for (int n=0;n<M;n++) {
        double t = (double)n / fs;
        double x = pow(r, t / dur_s);
        double phi = 2.0 * M_PI * (f0_hz * dur_s / ln_r) * (x - 1.0);
        refN[n] = (float)(amp * sin(phi));
    }
}
static int16_t be16_to_i16(const uint8_t hi, const uint8_t lo)
{
    uint16_t u = ((uint16_t)hi << 8) | (uint16_t)lo;
    return (int16_t)u;
}

/* abuf: [LH LL RH RL] * nsamp */
static void extract_lr_float_1m(const uint8_t* abuf, size_t abuf_len,
                                float* L, float* R, int N)
{
    /* Nサンプル欲しい → 4*N bytes 必要 */
    size_t need = (size_t)N * 4u;
    if (!abuf || abuf_len < need) {
        for (int i=0;i<N;i++){ L[i]=0.0f; R[i]=0.0f; }
        return;
    }

    for (int i=0;i<N;i++) {
        size_t o = (size_t)i * 4u;
        int16_t li = be16_to_i16(abuf[o+0], abuf[o+1]);
        int16_t ri = be16_to_i16(abuf[o+2], abuf[o+3]);

        /* 正規化は任意。まずは -1..1 に */
        L[i] = (float)li / 32768.0f;
        R[i] = (float)ri / 32768.0f;
    }
}

/* adc_read() を繰り返して指定バイト数まで読み切る（総タイムアウト付き） */
static int adc_read_exact(adc_port_t* adc, uint8_t* buf, size_t want, int total_timeout_ms)
{
    if (!adc || !buf || want == 0) return -1;

    size_t got = 0;
    int elapsed = 0;

    while (got < want) {
        int slice = ADC_SLICE_TIMEOUT_MS;
        if (elapsed + slice > total_timeout_ms) {
            slice = total_timeout_ms - elapsed;
            if (slice <= 0) break;
        }

        int n = adc_read(adc, buf + got, want - got, slice);
        if (n < 0) {
            return -1; /* read error */
        }
        if (n == 0) {
            /* timeout/no data in this slice */
            elapsed += slice;
            continue;
        }
        got += (size_t)n;
        /* データが来ている間は elapsed を進めない（実質“活動タイムアウト”にする） */
    }

    return (got == want) ? 0 : (int)got; /* 0=成功、>0=途中まで、<0=失敗 */
}

static void* adc_reader_thread(void* arg)
{
    adc_thread_ctx_t* ctx = (adc_thread_ctx_t*)arg;
    ctx->ok = 0;
    ctx->got = 0;

    int rc = adc_read_exact(ctx->adc, ctx->buf, ctx->want, ctx->total_timeout_ms);
    if (rc == 0) {
        ctx->ok = 1;
        ctx->got = ctx->want;
    } else if (rc > 0) {
        ctx->ok = 0;
        ctx->got = (size_t)rc;
    } else {
        ctx->ok = 0;
        ctx->got = 0;
    }
    return NULL;
}

static int save_bin(const char* path, const uint8_t* data, size_t len)
{
    FILE* f = fopen(path, "wb");
    if (!f) { perror("fopen bin"); return -1; }
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return (w == len) ? 0 : -1;
}

int main(void)
{
    /* (0) 出力フォルダ（無いとfopenで落ちる） */
    (void)system("mkdir -p output/pulse_data output/adc_data");

    /* ===== (A) CTRL疎通 ===== */
    ctrl_port_t* c2 = ctrl_open(CTRL_DEVICE_PATH, CTRL_BAUDRATE);
    if (!c2) { printf("ctrl_open failed (gain)\n"); return 1; }
    if (ctrl_send_line(c2, "g 300\n") != CTRL_OK) {
        printf("gain set failed\n");
        ctrl_close(c2);
        return 1;
    }
    ctrl_close(c2);
    printf("AMP gain set: g=300\n");

    /* ===== (B) パルス生成 ===== */
    size_t pb = 50000;              /* 40ms（あなたのデフォルト） */
    int freq_khz = 40;
    int duty_percent = 40;           /* 鳴る安全値。0にすれば全LOW確認にも使える */

    uint8_t* pbuf = (uint8_t*)malloc(pb);
    if (!pbuf) { printf("malloc failed\n"); return 1; }

    size_t wbytes = pulse_gen_pfd(pbuf, pb, freq_khz, duty_percent);
    if (wbytes == 0) {
        printf("pulse_gen_pfd failed (freq=%d duty=%d)\n", freq_khz, duty_percent);
        free(pbuf);
        return 1;
    }

    /* duty推定（全体の1比率） */
    unsigned long ones = 0;
    for (size_t i = 0; i < pb; i++) {
        uint8_t v = pbuf[i];
        for (int b = 0; b < 8; b++) ones += (v >> b) & 1u;
    }
    unsigned long bits = (unsigned long)pb * 8ul;
    printf("duty_est=%.2f%% (ones=%lu bits=%lu)\n",
           100.0 * (double)ones / (double)bits, ones, bits);

   
    /* 保存（0/1：ビット列確認用） */
    FILE *fb = fopen("output/pulse_data/pulse_raw_bits.txt", "w");
    if (!fb) { perror("fopen bits"); free(pbuf); return 1; }
    size_t total_bits = pb * 8;
    for (size_t bit = 0; bit < total_bits; bit++) {
        size_t byte_i = bit / 8;
        int bit_i = (int)(bit % 8); /* LSB first */
        int v = (pbuf[byte_i] >> bit_i) & 1;
        fputc(v ? '1' : '0', fb);
        if ((bit + 1) % 100 == 0) fputc('\n', fb);
    }
    fclose(fb);

    printf("Saved:\n");
    printf("  output/pulse_data/pulse_raw_real_device2.txt\n");

    /* ===== (C) ADC開始（パルスより先に読む準備） ===== */
    adc_port_t* adc = adc_open(ADC_DEVICE_PATH, ADC_BAUDRATE);
    if (!adc) {
        printf("adc_open failed (dev=%s)\n", ADC_DEVICE_PATH);
        free(pbuf);
        return 1;
    }
    if (adc_flush(adc) != ADC_OK) {
        printf("adc_flush failed\n");
        adc_close(adc);
        free(pbuf);
        return 1;
    }

    uint8_t* abuf = (uint8_t*)malloc(ADC_READ_BYTES);
    if (!abuf) {
        printf("malloc failed (abuf)\n");
        adc_close(adc);
        free(pbuf);
        return 1;
    }
    memset(abuf, 0, ADC_READ_BYTES);

    adc_thread_ctx_t actx;
    memset(&actx, 0, sizeof(actx));
    actx.adc = adc;
    actx.buf = abuf;
    actx.want = ADC_READ_BYTES;
    actx.total_timeout_ms = ADC_TIMEOUT_MS;

    pthread_t th;
    if (pthread_create(&th, NULL, adc_reader_thread, &actx) != 0) {
        printf("pthread_create failed\n");
        free(abuf);
        adc_close(adc);
        free(pbuf);
        return 1;
    }

    /* ===== (D) パルス送信（PortA） ===== */
    pulse_port_t* pulse = pulse_open(PULSE_DEVICE_PATH, PULSE_BAUDRATE);
    if (!pulse) {
        printf("pulse_open failed (dev=%s)\n", PULSE_DEVICE_PATH);
        pthread_join(th, NULL);
        free(abuf);
        adc_close(adc);
        free(pbuf);
        return 1;
    }

    pulse_result_t pr = pulse_write(pulse, pbuf, wbytes);
    if (pr != PULSE_OK) {
        printf("pulse_write failed\n");
        pulse_close(pulse);
        pthread_join(th, NULL);
        free(abuf);
        adc_close(adc);
        free(pbuf);
        return 1;
    }

    pulse_close(pulse);
    printf("pulse_write OK (%zu bytes)\n", wbytes);

    /* ===== (E) ADC完了待ち ===== */
    pthread_join(th, NULL);

    if (actx.ok) {
        printf("ADC read OK (%zu bytes)\n", actx.got);
    } else {
        printf("ADC read NOT complete (got=%zu want=%zu)\n", actx.got, actx.want);
    }

    /* ===== (F) ADC生データ保存 ===== */
    if (actx.got > 0) {
        if (save_bin("output/adc_data/adc_dump_FMsound_test2.bin", abuf, actx.got) != 0) {
            printf("save adc_dump.bin failed\n");
        } else {
            printf("Saved:\n");
            printf("  output/adc_data/adc_dump_FMsound_test2.bin\n");
        }
    } else {
        printf("ADC got 0 bytes (nothing to save)\n");
    }

    /* ===== (G) Cross-correlation (L/R) ===== */
    printf("DEBUG: after save, got=%zu\n", actx.got);

    if (actx.got >= 4u * 32768u) {
        printf("DEBUG: enter XCORR block\n");
        const int N = 32768;
        const double FS = 1000000.0;
        const double HPF = 35000.0;

        float* ref = (float*)malloc(sizeof(float) * (size_t)N);
        float* L = (float*)malloc(sizeof(float) * (size_t)N);
        float* R = (float*)malloc(sizeof(float) * (size_t)N);
        float* envL = (float*)malloc(sizeof(float) * (size_t)N);
        float* envR = (float*)malloc(sizeof(float) * (size_t)N);

        if (!ref || !L || !R || !envL || !envR) {
            printf("xcorr malloc failed\n");
        } else {
            /* 参照：95k -> 50k, 8ms */
            gen_ref_exp_chirp_1m(ref, N, FS, 95000.0, 50000.0, 0.008, 1.0);

            xcorr_ctx_t* xc = xcorr_create(N, FS, HPF);
            if (!xc) {
                printf("xcorr_create failed (fftw)\n");
            } else {
                xcorr_set_call_time(xc, ref);

                /* まずは先頭から Nサンプル切り出し（後でオフセット調整する） */
                extract_lr_float_1m(abuf, actx.got, L, R, N);

                xcorr_run_envelope(xc, L, envL);
                xcorr_run_envelope(xc, R, envR);

                /* 距離範囲（あなたの設定）: 0.51m〜2.55m
                round-trip samples: t=2d/c, samples=t*Fs */
                const double c = 340.0;      /* m/s */
                const double mic = 0.116;    /* m */
                size_t i0 = (size_t)( (2.0*0.51 / c) * FS );  /* ~3000 */
                size_t i1 = (size_t)( (2.0*2.55 / c) * FS );  /* ~15000 */
                if (i1 > (size_t)N) i1 = (size_t)N;

                size_t lTime = xcorr_argmax_range(envL, (size_t)N, i0, i1);
                size_t rTime = xcorr_argmax_range(envR, (size_t)N, i0, i1);

                double tL = (double)lTime / FS;
                double tR = (double)rTime / FS;

                /* 距離(m): (tL+tR)*c/4 */
                double dist = (tL + tR) * c / 4.0;

                /* 角度(rad): asin((tL-tR)*c/mic) */
                double s = (tL - tR) * c / mic;
                if (s > 1.0) s = 1.0;
                if (s < -1.0) s = -1.0;
                double theta = asin(s);
                double deg = theta * (180.0 / M_PI);

                printf("XCORR peak: L=%zu R=%zu (range[%zu..%zu))\n", lTime, rTime, i0, i1);
                printf("XCORR result: dist=%.3f m, angle=%.1f deg\n", dist, deg);

                /* 必要なら env を保存（まずは軽く左だけ） */
                FILE* fe = fopen("output/adc_data/xcorr_envL.txt", "w");
                if (fe) {
                    for (int i=0;i<N;i++) fprintf(fe, "%d %.6f\n", i, envL[i]);
                    fclose(fe);
                    printf("Saved: output/adc_data/xcorr_envL.txt\n");
                }

                xcorr_destroy(xc);
            }
        }

        free(ref); free(L); free(R); free(envL); free(envR);
    } else {
        printf("XCORR skipped: need at least %u bytes, got=%zu\n", 4u*32768u, actx.got);
    }


    /* 後片付け */
    free(abuf);
    adc_close(adc);
    free(pbuf);
    return 0;
}
