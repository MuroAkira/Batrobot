#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "config.h"
#include "ctrl_port.h"
#include "pulse_port.h"

int main(void)
{
    /* CTRL疎通は残してOK（なくても可） */
    ctrl_port_t* ctrl = ctrl_open(CTRL_DEVICE_PATH, CTRL_BAUDRATE);
    if (!ctrl) { printf("ctrl_open failed\n"); return 1; }

    if (ctrl_enq(ctrl) != CTRL_OK) {
        printf("ENQ/ACK NG\n");
        ctrl_close(ctrl);
        return 1;
    }
    printf("ENQ/ACK OK\n");
    ctrl_close(ctrl);

    /* ===== ここから「生成→保存」だけ ===== */

    size_t pb = 20000;                 /* まず小さく（安全確認用） */
    int freq_khz = 40;
    int duty_percent = 5;           /* 今はduty=0で確認 */

    uint8_t* pbuf = (uint8_t*)malloc(pb);
    if (!pbuf) { printf("malloc failed\n"); return 1; }

    /* duty=0 に対応していない場合は pulse_gen_pfd 側を修正が必要 */
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

    // /* 保存（16進：バイト列確認用） */
    // FILE *fh = fopen("output/pulse_data/pulse_raw.hex", "w");
    // if (!fh) { perror("fopen hex"); free(pbuf); return 1; }
    // for (size_t i = 0; i < pb; i++) {
    //     fprintf(fh, "%02X%s", pbuf[i], ((i + 1) % 16 == 0) ? "\n" : " ");
    // }
    // fclose(fh);

    /* 保存（0/1：ビット列確認用） */
    FILE *fb = fopen("output/pulse_data/pulse_raw_real_device1.txt", "w");
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
    printf("  output/pulse_data/pulse_raw.hex\n");
    printf("  output/pulse_data/pulse_raw_bits.txt\n");
    /* ===== ここから実機送信（PortA） ===== */
    pulse_port_t* pulse = pulse_open(PULSE_DEVICE_PATH, PULSE_BAUDRATE);
    if (!pulse) {
        printf("pulse_open failed (dev=%s)\n", PULSE_DEVICE_PATH);
        free(pbuf);
        return 1;
    }

    pulse_result_t pr = pulse_write(pulse, pbuf, wbytes);
    if (pr != PULSE_OK) {
        printf("pulse_write failed\n");
        pulse_close(pulse);
        free(pbuf);
        return 1;
    }

    /* 送信完了待ち（必要なら：termiosの送信FIFO吐き出し） */
    /* tcdrain(pulse_fd) は pulse_port.c 側に入れるのが筋。
       まずは無しでOK。 */

    pulse_close(pulse);
    printf("pulse_write OK (%zu bytes)\n", wbytes);

    free(pbuf);
    return 0;
}
