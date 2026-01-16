// #include <stdio.h>
// #include "ctrl_port.h"
// #include "config.h"

// int main(void)
// {
//     ctrl_port_t* ctrl = NULL;
//     uint32_t hz = 0;

//     /* 制御ポートを開く（※未実装） */
//     ctrl = ctrl_open(CTRL_DEVICE_PATH, CTRL_BAUDRATE);
//     if (!ctrl) {
//         printf("ctrl_open failed\n");
//         return 1;
//     }

//     /* 疎通確認 */
//     if (ctrl_enq(ctrl) != CTRL_OK) {
//         printf("ENQ/ACK failed\n");
//         ctrl_close(ctrl);
//         return 1;
//     }

//     /* サンプリング周波数を取得 */
//     if (ctrl_get_sampling_hz(ctrl, &hz) == CTRL_OK) {
//         printf("sampling hz = %u\n", hz);
//     }

//     ctrl_close(ctrl);
//     return 0;
// }

/* 受信：ビルド確認用 */
// #include <stdio.h>
// #include <stdint.h>

// #include "config.h"
// #include "ctrl_port.h"
// #include "adc_port.h"

// int main(void)
// {
//     /* 制御ポート疎通（音なし） */
//     ctrl_port_t* ctrl = ctrl_open(CTRL_DEVICE_PATH, CTRL_BAUDRATE);
//     if (!ctrl) {
//         printf("ctrl_open failed\n");
//         return 1;
//     }
//     if (ctrl_enq(ctrl) != CTRL_OK) {
//         printf("ENQ/ACK NG\n");
//         ctrl_close(ctrl);
//         return 1;
//     }
//     printf("ENQ/ACK OK\n");
//     ctrl_close(ctrl);

//     /* ADCポート（読むだけ、音なし） */
//     adc_port_t* adc = adc_open(ADC_DEVICE_PATH, ADC_BAUDRATE);
//     if (!adc) {
//         printf("adc_open failed\n");
//         return 1;
//     }
//     adc_flush(adc);

//     uint8_t buf[256];
//     int n = adc_read(adc, buf, sizeof(buf), 500);
//     printf("adc_read bytes=%d\n", n);

//     adc_close(adc);
//     return 0;
// }


/* 送信：ビルド確認用 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "config.h"
#include "ctrl_port.h"
#include "adc_port.h"
#include "pulse_port.h"

int main(void)
{
    ctrl_port_t* ctrl = ctrl_open(CTRL_DEVICE_PATH, CTRL_BAUDRATE);
    if (!ctrl) { printf("ctrl_open failed\n"); return 1; }

    if (ctrl_enq(ctrl) != CTRL_OK) {
        printf("ENQ/ACK NG\n");
        ctrl_close(ctrl);
        return 1;
    }
    printf("ENQ/ACK OK\n");
    ctrl_close(ctrl);

    /* PULSE: デフォルト例（とりあえず 40kHz, duty 10% で安全寄り） */
    pulse_port_t* pulse = pulse_open(PULSE_DEVICE_PATH, PULSE_BAUDRATE);
    if (!pulse) { printf("pulse_open failed\n"); return 1; }

    size_t pb = 50000; /* TPS-mainのデフォルト例 :contentReference[oaicite:4]{index=4} */
    uint8_t* pbuf = (uint8_t*)malloc(pb);
    if (!pbuf) { printf("malloc failed\n"); pulse_close(pulse); return 1; }

    pulse_gen_pfd(pbuf, pb, 40 /*kHz*/, 10 /*%*/);
    /* DEBUG: duty推定（全体の1比率） */
    unsigned long ones = 0;
    for (size_t i = 0; i < pb; i++) {
        uint8_t v = pbuf[i];
        for (int b = 0; b < 8; b++) ones += (v >> b) & 1u;
    }
    unsigned long bits = (unsigned long)pb * 8ul;
    printf("duty_est=%.2f%% (ones=%lu bits=%lu)\n", 100.0 * (double)ones / (double)bits, ones, bits);

    /* ==== DEBUG: PULSE生データをファイルに保存 ==== */
    FILE *fp = fopen("output/pulse_data/pulse_raw_test2.txt", "w");
    if (fp) {
        for (size_t i = 0; i < pb; i++) {
            /* 16進で1byteずつ出力 */
            fprintf(fp, "%02X", pbuf[i]);

            /* 見やすさのため、16byteごとに改行 */
            if ((i + 1) % 16 == 0) {
                fprintf(fp, "\n");
            } else {
                fprintf(fp, " ");
            }
        }
        fclose(fp);
        printf("pulse_raw.txt written (%zu bytes)\n", pb);
    } else {
        perror("fopen pulse_raw.txt");
    }

    /* ==== DEBUG: PULSE生データを bit(0/1) で保存 ==== */
    FILE *fb = fopen("output/pulse_data/pulse_raw_test2.txt", "w");
    if (fb) {
        size_t total_bits = pb * 8;
        for (size_t bit = 0; bit < total_bits; bit++) {
            size_t byte_i = bit / 8;
            int bit_i = bit % 8;          /* LSB first */
            int v = (pbuf[byte_i] >> bit_i) & 1;

            fprintf(fb, "%d", v);

            /* 見やすさ：100bit = 10us ごとに改行 */
            if ((bit + 1) % 100 == 0) {
                fprintf(fb, "\n");
            }
        }
        fclose(fb);
        printf("pulse_raw_bits.txt written (%zu bits)\n", total_bits);
    } else {
        perror("fopen pulse_raw_bits.txt");
    }
    /* ==== DEBUG END ==== */

    /* ==== DEBUG END ==== */



    if (pulse_write(pulse, pbuf, pb) != PULSE_OK) {
        printf("pulse_write failed\n");
        free(pbuf);
        pulse_close(pulse);
        return 1;
    }
    printf("pulse_write OK bytes=%zu\n", pb);
    free(pbuf);
    pulse_close(pulse);

    /* ADC */
    adc_port_t* adc = adc_open(ADC_DEVICE_PATH, ADC_BAUDRATE);
    if (!adc) { printf("adc_open failed\n"); return 1; }
    adc_flush(adc);

    uint8_t abuf[256];
    int n = adc_read(adc, abuf, sizeof(abuf), 500);
    printf("adc_read bytes=%d\n", n);

    adc_close(adc);
    return 0;
}
/* 送信：ビルド確認用 */