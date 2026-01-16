// サーモホンを鳴らすパルス送信ポートのヘッダーファイル
#ifndef PULSE_PORT_H
#define PULSE_PORT_H

#include <stdint.h>
#include <stddef.h>

typedef struct pulse_port pulse_port_t;

typedef enum {
    PULSE_OK = 0,
    PULSE_ERR = -1
} pulse_result_t;

pulse_port_t* pulse_open(const char* devpath, int baudrate);
void pulse_close(pulse_port_t* p);

/* 仮想ポート専用の送信（安全ロック強） */
pulse_result_t pulse_write_locked(pulse_port_t* p, const uint8_t* data, size_t len);

/* 安全ゲート付き送信（is_safe_devpath() の許可先のみ送信） */
pulse_result_t pulse_write(pulse_port_t* p, const uint8_t* data, size_t len);

/* 矩形波生成：freq_khz(1..5000), duty_percent(0..99)
   10MHz基準: 1bit=0.1us, LSB first */
size_t pulse_gen_pfd(uint8_t* out, size_t out_bytes, int freq_khz, int duty_percent);

#endif /* PULSE_PORT_H */
