#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "config.h"
#include "ctrl_port.h"
#include "pulse_port.h"
#include "adc_port.h"

/* ====== ADC設定（まずはここだけ調整すればOK） ======
   どれだけ読むかは基板側の設定（read_bytes等）と合わせる必要があります */
#ifndef ADC_READ_BYTES
#define ADC_READ_BYTES (256000)     /* 64ms fs 1Mhz */
#endif

#ifndef ADC_TIMEOUT_MS
#define ADC_TIMEOUT_MS (1000)      /* 1000ms */
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
    ctrl_port_t* ctrl = ctrl_open(CTRL_DEVICE_PATH, CTRL_BAUDRATE);
    if (!ctrl) { printf("ctrl_open failed\n"); return 1; }

    if (ctrl_enq(ctrl) != CTRL_OK) {
        printf("ENQ/ACK NG\n");
        ctrl_close(ctrl);
        return 1;
    }
    printf("ENQ/ACK OK\n");
    ctrl_close(ctrl);

    /* ===== (B) パルス生成 ===== */
    size_t pb = 50000;              /* 40ms（あなたのデフォルト） */
    int freq_khz = 40;
    int duty_percent = 5;           /* 鳴る安全値。0にすれば全LOW確認にも使える */

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
        if (save_bin("output/adc_data/adc_dump_test1.bin", abuf, actx.got) != 0) {
            printf("save adc_dump.bin failed\n");
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
