#ifndef PULSE_PORT_H
#define PULSE_PORT_H

#include <stdint.h>
#include <stddef.h>

typedef struct pulse_port pulse_port_t;

typedef enum {
    PULSE_OK = 0,
    PULSE_ERR = -1
} pulse_result_t;

/* open/close だけは安全 */
pulse_port_t* pulse_open(const char* devpath, int baudrate);
void pulse_close(pulse_port_t* p);

/*
 * 音が出る可能性があるため “安全ロック” をかける。
 * 実行は後日、あなたの許可のもとで ENABLE_SOUND を定義したビルドだけに限定する。
 */
pulse_result_t pulse_write_locked(pulse_port_t* p, const uint8_t* data, size_t len);

#endif
