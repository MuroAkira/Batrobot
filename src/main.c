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


/* ビルド確認用 */


/* 送信：ビルド確認用 */
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    fprintf(stderr,
        "SAFETY LOCK: This program must NOT be executed.\n"
        "Build is allowed, execution is forbidden in this phase.\n");
    return 2;
}
/* 送信：ビルド確認用 */