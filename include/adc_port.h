#ifndef ADC_PORT_H
#define ADC_PORT_H

#include <stdint.h>
#include <stddef.h>

typedef struct adc_port adc_port_t;

typedef enum {
    ADC_OK = 0,
    ADC_ERR = -1
} adc_result_t;

adc_port_t* adc_open(const char* devpath, int baudrate);
void adc_close(adc_port_t* adc);

/* 受信（最大lenバイト）。timeout_msで待つ。戻り値は読めたバイト数、失敗は-1 */
int adc_read(adc_port_t* adc, uint8_t* buf, size_t len, int timeout_ms);

/* 入力バッファを捨てる（残ゴミ対策） */
adc_result_t adc_flush(adc_port_t* adc);

#endif
