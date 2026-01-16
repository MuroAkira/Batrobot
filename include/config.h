#ifndef CONFIG_H
#define CONFIG_H

#define ADC_DEVICE_PATH    "/tmp/ADC_A"
#define ADC_BAUDRATE       115200

// #define PULSE_DEVICE_PATH  "/dev/tmp/PULSE_A"//仮想socatポート
#define PULSE_DEVICE_PATH  "/dev/ttyUSB0"//実機
#define PULSE_BAUDRATE     115200

/* ===== デバイスパス ===== */
// #define CTRL_DEVICE_PATH   "/tmp/CTRL_A"//仮想socatポート
#define CTRL_DEVICE_PATH "/dev/ttyUSB2"//実機

/* ===== 通信設定 バウンドレート ===== */
#define CTRL_BAUDRATE      115200

/* ===== 制御系の制限 ===== */
#define CTRL_TIMEOUT_MS    1000

#endif /* CONFIG_H */
