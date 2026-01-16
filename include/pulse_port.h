//サーモホンを鳴らすパルス送信ポートのヘッダーファイル

#ifndef PULSE_PORT_H //ヘッダガード
#define PULSE_PORT_H

#include <stdint.h>//基本型の準備
#include <stddef.h>

typedef struct pulse_port pulse_port_t;// pulse_port_t型を不完全型として宣言


typedef enum {
    PULSE_OK = 0,
    PULSE_ERR = -1
} pulse_result_t;//戻り値の型定義 関数が成功したか失敗したかを返すための型

pulse_port_t* pulse_open(const char* devpath, int baudrate);
void pulse_close(pulse_port_t* p);

/* /tmp/PULSE_A のときだけ送信OK（実機デバイスは拒否） */
pulse_result_t pulse_write_locked(pulse_port_t* p, const uint8_t* data, size_t len);

/* pfd相当：freq_khz(1..5000), duty_percent(1..99) の矩形波を bit列にして埋める */
size_t pulse_gen_pfd(uint8_t* out, size_t out_bytes, int freq_khz, int duty_percent);

#endif
