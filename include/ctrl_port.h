#ifndef CTRL_PORT_H
#define CTRL_PORT_H

#include <stdint.h>
#include <stddef.h>

/*
 * ctrl_port: 制御ポート（CP210x）専用API
 * ポート: /dev/ttyUSB2
 * 役割: ENQ/ACK、制御コマンド送受信
 */


/* struct ctrl_port という構造体をctrl_port_t という名前で使えるようにする*/
typedef struct ctrl_port ctrl_port_t;

/* ===== ライフサイクル ===== */
/**
 * 制御ポートを開く
 * @param devpath 例: "/dev/ttyUSB2"
 * @param baudrate 例: 115200
 * @return 成功時: 非NULL / 失敗時: NULL
 */
ctrl_port_t* ctrl_open(const char* devpath, int baudrate);

/**
 * 制御ポートを閉じる
 * @param ctrl ctrl_open で得たハンドル
 */
void ctrl_close(ctrl_port_t* ctrl);

/* ===== 疎通確認 ===== */
/**
 * ENQ(0x05) を送信し ACK(0x06) を確認
 * @return 0: OK / -1: NG
 */


/* ===== 設定・取得コマンド ===== */
/**
 * サンプリング周波数を取得（f）
 * @param hz_out 取得したHzを格納
 * @return 0: OK / -1: NG
 */


/**
 * サンプリング周波数を設定（f <value>）
 * @param hz 設定するHz
 * @return 0: OK / -1: NG
 */
int ctrl_set_sampling_hz(ctrl_port_t* ctrl, uint32_t hz);

/**
 * アンプゲインを設定（g <value>）
 * @param gain ゲイン値
 * @return 0: OK / -1: NG
 */
int ctrl_set_gain(ctrl_port_t* ctrl, uint32_t gain);

/**
 * エラーカウンタ取得（e）
 * @param err_out エラーカウンタ
 * @return 0: OK / -1: NG
 */
int ctrl_get_error_count(ctrl_port_t* ctrl, uint32_t* err_out);

typedef enum {
    CTRL_OK = 0,
    CTRL_ERR = -1
} ctrl_result_t;

ctrl_result_t ctrl_enq(ctrl_port_t* ctrl);

ctrl_result_t ctrl_get_sampling_hz(ctrl_port_t* ctrl, uint32_t* hz_out);

ctrl_result_t ctrl_send_line(ctrl_port_t* ctrl, const char* line);
ctrl_result_t ctrl_get_errors(ctrl_port_t* ctrl, uint32_t* pulse_err, uint32_t* adc_err);

#endif /* CTRL_PORT_H */
