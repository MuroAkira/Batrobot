#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "config.h"
#include "ctrl_port.h"
#include "pulse_port.h"
#include "adc_port.h"

/* ====== ADC設定 ======
   ADC_READ_BYTES は基板側の設定（read_bytes等）と合わせる */
#ifndef ADC_READ_BYTES
#define ADC_READ_BYTES (256000)     /* 64ms @ 1MHz, 4byte/sample -> 256000 bytes */
#endif

#ifndef ADC_START_TIMEOUT_MS
#define ADC_START_TIMEOUT_MS (500)  /* 最初の1byte待ち */
#endif

#ifndef ADC_IDLE_TIMEOUT_MS
#define ADC_IDLE_TIMEOUT_MS  (2000) /* 途中でデータが途切れたら失敗 */
#endif

typedef struct {
    adc_port_t* adc;
    uint8_t* buf;
    size_t want;
    size_t got;
    int ok;             /* 1=成功 */
} adc_thread_ctx_t;

/* 指定バイト数を「開始待ち + 活動タイムアウト」で読み切る
   戻り値: want(成功) / 0..want-1(途中まで) / -1(エラー) */
static int adc_read_exact(adc_port_t* adc, uint8_t* buf, size_t want,
                          int start_timeout_ms, int idle_timeout_ms)
{
    if (!adc || !buf || want == 0) return -1;

    size_t got = 0;

    /* 1) 開始待ち：最初のデータが来るまで */
    int n = adc_read(adc, buf, want, start_timeout_ms);
    if (n < 0) return -1;
    if (n == 0) return 0;          /* 何も来なかった */
    got += (size_t)n;

    /* 2) 活動タイムアウト：データが来ている間は継続、途切れたら終了 */
    while (got < want) {
        int m = adc_read(adc, buf + got, want - got, idle_timeout_ms);
        if (m < 0) return -1;
        if (m == 0) return (int)got;  /* 途中で途切れた */
        got += (size_t)m;
    }

    return (int)got;
}

static void* adc_reader_thread(void* arg)
{
    adc_thread_ctx_t* ctx = (adc_thread_ctx_t*)arg;
    ctx->ok = 0;
    ctx->got = 0;

    int rc = adc_read_exact(ctx->adc, ctx->buf, ctx->want,
                            ADC_START_TIMEOUT_MS, ADC_IDLE_TIMEOUT_MS);

    if (rc == (int)ctx->want) {  // 成功
        ctx->ok = 1;
        ctx->got = ctx->want;
    } else if (rc >= 0) {        // 未完了（途中まで）
        ctx->ok = 0;
        ctx->got = (size_t)rc;
    } else {                     // エラー
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
    /* (0) 出力フォルダ */
    (void)system("mkdir -p output/pulse_data output/adc_data");

    /* ===== (A) CTRL：ゲイン設定 ===== */
    ctrl_port_t* c2 = ctrl_open(CTRL_DEVICE_PATH, CTRL_BAUDRATE);
    if (!c2) { printf("ctrl_open failed (gain)\n"); return 1; }
    if (ctrl_send_line(c2, "g 300\n") != CTRL_OK) {
        printf("gain set failed\n");
        ctrl_close(c2);
        return 1;
    }
    ctrl_close(c2);
    printf("AMP gain set: g=300\n");

    /* ===== (B) PULSE生成（CF/FＭ切替） ===== */
    typedef enum { MODE_CF, MODE_FM } mode_t;
    mode_t mode = MODE_FM;   /* ここ一行で切替 */
    printf("PULSE mode: %s\n", (mode==MODE_FM) ? "FM" : "CF");

    /* 10MHz bit clock */
    const double FS_BIT = 10e6;

    /* FM設定 */
    double dur = 0.008;          /* 8ms */
    double f_start = 95000.0;    /* 95kHz */
    double f_end   = 50000.0;    /* 50kHz */

    /* duty（安全ゲート60%未満で） */
    int duty_percent = 10;

    size_t pb;
    size_t wbytes = 0;

    /* ① pb を決める */
    if (mode == MODE_CF) {
        pb = 50000; /* 40ms固定（従来） */
    } else {
        pb = pulse_bytes_for_duration(FS_BIT, dur); /* 8ms -> 10000 bytes */
    }

    /* ② 確保 */
    uint8_t* pbuf = (uint8_t*)malloc(pb);
    if (!pbuf) { perror("malloc pbuf"); return 1; }

    /* ③ 生成 */
    if (mode == MODE_CF) {
        int freq_khz = 40;
        wbytes = pulse_gen_pfd(pbuf, pb, freq_khz, duty_percent);
    } else {
        wbytes = pulse_gen_exp_chirp(pbuf, pb, FS_BIT, dur,
                                     f_start, f_end, duty_percent);
    }

    if (wbytes == 0) {
        printf("pulse_gen failed\n");
        free(pbuf);
        return 1;
    }

    /* duty推定（全体の1比率） */
    unsigned long ones = 0;
    for (size_t i = 0; i < wbytes; i++) {
        uint8_t v = pbuf[i];
        for (int b = 0; b < 8; b++) ones += (v >> b) & 1u;
    }
    unsigned long bits = (unsigned long)wbytes * 8ul;
    printf("duty_est=%.2f%% (ones=%lu bits=%lu)\n",
           100.0 * (double)ones / (double)bits, ones, bits);

    /* ==== パルス生データ保存（確認用） ==== */
    FILE* fp = fopen("output/pulse_data/pulse_bytes.bin", "wb");
    if (!fp) { perror("fopen pulse_bytes.bin"); free(pbuf); return 1; }
    fwrite(pbuf, 1, wbytes, fp);
    fclose(fp);

    /* ビット列も保存（LSB first） */
    FILE* fb = fopen("output/pulse_data/pulse_bits.txt", "w");
    if (!fb) { perror("fopen pulse_bits.txt"); free(pbuf); return 1; }
    for (size_t bit = 0; bit < wbytes * 8; bit++) {
        size_t byte_i = bit / 8;
        int bit_i = (int)(bit % 8);
        int v = (pbuf[byte_i] >> bit_i) & 1;
        fputc(v ? '1' : '0', fb);
        if ((bit + 1) % 100 == 0) fputc('\n', fb);
    }
    fclose(fb);

    printf("Saved pulse:\n");
    printf("  output/pulse_data/pulse_bytes.bin\n");
    printf("  output/pulse_data/pulse_bits.txt\n");

    /* ===== (C) ADC開始（先に受信スレッド） ===== */
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

    pthread_t th;
    if (pthread_create(&th, NULL, adc_reader_thread, &actx) != 0) {
        printf("pthread_create failed\n");
        free(abuf);
        adc_close(adc);
        free(pbuf);
        return 1;
    }

    /* ===== (C2) エラー before ===== */
    uint32_t pe0=0, ae0=0, pe1=0, ae1=0;
    ctrl_port_t* ce0 = ctrl_open(CTRL_DEVICE_PATH, CTRL_BAUDRATE);
    if (ce0 && ctrl_get_errors(ce0, &pe0, &ae0) == CTRL_OK) {
        printf("ERR(before): pulse=%u adc=%u\n", pe0, ae0);
    }
    if (ce0) ctrl_close(ce0);

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

    /* ===== (E2) エラー after ===== */
    ctrl_port_t* ce1 = ctrl_open(CTRL_DEVICE_PATH, CTRL_BAUDRATE);
    if (ce1 && ctrl_get_errors(ce1, &pe1, &ae1) == CTRL_OK) {
        printf("ERR(after):  pulse=%u adc=%u\n", pe1, ae1);
        printf("ERR(delta):  pulse=%d adc=%d\n", (int)(pe1-pe0), (int)(ae1-ae0));
    }
    if (ce1) ctrl_close(ce1);

    if (actx.ok) {
        printf("ADC read OK (%zu bytes)\n", actx.got);
    } else {
        printf("ADC read NOT complete (got=%zu want=%zu)\n", actx.got, actx.want);
    }

    /* ===== (F) ADC生データ保存 ===== */
    if (actx.got > 0) {
        if (save_bin("output/adc_data/adc_FM_test5.bin", abuf, actx.got) != 0) {
            printf("save adc_FM_test.bin failed\n");
        } else {
            printf("Saved:\n");
            printf("  output/adc_data/adc_dump.bin\n");
        }
    } else {
        printf("ADC got 0 bytes (nothing to save)\n");
    }

    /* 後片付け */
    free(abuf);
    adc_close(adc);
    free(pbuf);
    return 0;
}
