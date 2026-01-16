#include "pulse_port.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

struct pulse_port {
    int fd;
    char devpath[256];
};

static speed_t baud_to_flag(int baudrate)// ボーレートを対応するtermiosの速度フラグに変換する関数
{
    switch (baudrate) {
    case 115200: return B115200;
    default:     return 0;
    }
}

static int is_safe_devpath(const char* p)
{
    if (!p) return 0;

    /* 仮想ポートも許可 */
    if (strncmp(p, "/tmp/PULSE_", 11) == 0) return 1;

    /* 実機テストはPortA(/dev/ttyUSB0)だけ許可 */
    if (strcmp(p, "/dev/ttyUSB0") == 0) return 1;

    return 0;
}


pulse_port_t* pulse_open(const char* devpath, int baudrate)
{
    if (!devpath) return NULL;

    speed_t sp = baud_to_flag(baudrate);
    if (sp == 0) return NULL;

    int fd = open(devpath, O_RDWR | O_NOCTTY );
    if (fd < 0) return NULL;

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return NULL;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);

    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;

    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return NULL;
    }

    pulse_port_t* p = (pulse_port_t*)malloc(sizeof(pulse_port_t));
    if (!p) {
        close(fd);
        return NULL;
    }
    p->fd = fd;
    snprintf(p->devpath, sizeof(p->devpath), "%s", devpath);
    return p;
}

void pulse_close(pulse_port_t* p)
{
    if (!p) return;
    if (p->fd >= 0) close(p->fd);
    free(p);
}

/* pfd相当の矩形波：10MHz(=0.1us)基準で作る。
   freq_khz: 1..5000
   duty%   : 1..99
   1bit = 0.1us, 1byte = 8bit */
size_t pulse_gen_pfd(uint8_t* out, size_t out_bytes, int freq_khz, int duty_percent)
{
    if (!out || out_bytes == 0) return 0;
    if (freq_khz < 1 || freq_khz > 5000) return 0;
    if (duty_percent < 0 || duty_percent > 99) return 0;

    if (duty_percent == 0) {
    memset(out, 0x00, out_bytes);
    return out_bytes;}

    /* 10MHz基準 → 1周期のtick数は 10000/freq_khz （0.1us単位） */
    int period_ticks = (10000 + freq_khz/2) / freq_khz;   /* 四捨五入 */
    if (period_ticks < 1) period_ticks = 1;

    int on_ticks = (period_ticks * duty_percent + 50) / 100;
    if (on_ticks < 1) on_ticks = 1;
    if (on_ticks >= period_ticks) on_ticks = period_ticks - 1;
    fprintf(stderr, "pulse_gen_pfd: freq_khz=%d period_ticks=%d on_ticks=%d (duty=%.2f%%)\n",freq_khz, period_ticks, on_ticks, 100.0 * (double)on_ticks / (double)period_ticks);
    memset(out, 0x00, out_bytes);

    size_t total_bits = out_bytes * 8;
    for (size_t bit = 0; bit < total_bits; bit++) {
        int phase = (int)(bit % (size_t)period_ticks);
        int v = (phase < on_ticks) ? 1 : 0;

        if (v) {
            size_t byte_i = bit / 8;
            int bit_i = (int)(bit % 8);
            out[byte_i] |= (uint8_t)(1u << bit_i);
        }
    }
    return out_bytes;
}

/* data内の連続1ビットの最長長を数える（送信前チェック用） */
static int max_consecutive_ones_bits(const uint8_t* data, size_t len)
{
    int max_run = 0, run = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t v = data[i];
        for (int b = 0; b < 8; b++) {
            int bit = (v >> b) & 1u;
            if (bit) { run++; if (run > max_run) max_run = run; }
            else run = 0;
        }
    }
    return max_run;
}


pulse_result_t pulse_write_locked(pulse_port_t* p, const uint8_t* data, size_t len)
{
    int max_run = max_consecutive_ones_bits(data, len);
    if (max_run >= 200) {
        fprintf(stderr, "PULSE blocked: too long HIGH run=%d bits\n", max_run);
        return PULSE_ERR;
    }
    ssize_t w = write(p->fd, data, len);
    return (w == (ssize_t)len) ? PULSE_OK : PULSE_ERR;
}

#include <string.h>
#include <stdlib.h>

pulse_result_t pulse_write(pulse_port_t* p, const uint8_t* data, size_t len)
{
    if (!p || p->fd < 0 || !data || len == 0) return PULSE_ERR;

    /* 実機へは絶対に流さない（仮想ポートのみ） */
    if (!is_safe_devpath(p->devpath)) {
        fprintf(stderr, "PULSE locked: devpath=%s\n", p->devpath);
        return PULSE_ERR;
    }

    /* 安全: 送信長（仮想テスト用。必要なら後でconfig化） */
    if (len > 50000) {
        fprintf(stderr, "PULSE blocked: len too long (%zu)\n", len);
        return PULSE_ERR;
    }

    /* ===== Safety gate ===== */

    /* duty 推定（全ビットの1比率） */
    unsigned long ones = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t v = data[i];
        for (int b = 0; b < 8; b++) ones += (v >> b) & 1u;
    }
    unsigned long bits = (unsigned long)len * 8ul;
    double duty_est = 100.0 * (double)ones / (double)bits;

    /* 連続 High の最大長（LSB first） */
    int max_run = 0;
    int run = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t v = data[i];
        for (int b = 0; b < 8; b++) {
            if ((v >> b) & 1u) {
                run++;
                if (run > max_run) max_run = run;
            } else {
                run = 0;
            }
        }
    }

    /* ログ（安全確認の証跡） */
    fprintf(stderr, "PULSE safety: len=%zu duty_est=%.2f%% max_run=%d bits\n",
            len, duty_est, max_run);

    /* 安全: duty 60%以上は拒否（= 59%まで許可） */
    if (duty_est >= 60.0) {
        fprintf(stderr, "PULSE blocked: duty >= 60%%\n");
        return PULSE_ERR;
    }

    /* 安全: 連続Highが長すぎるのも拒否（20us以上の連続Highを止める） */
    if (max_run >= 200) { /* 200bit = 20us (10MHz基準: 1bit=0.1us) */
        fprintf(stderr, "PULSE blocked: max_run too long (%d bits)\n", max_run);
        return PULSE_ERR;
    }

    /* ===== Safety gate end ===== */

    /* ここまで来たら送信 */
    ssize_t w = write(p->fd, data, len);
    if (w != (ssize_t)len) {
        fprintf(stderr, "PULSE write failed: w=%zd expected=%zu errno=%d\n", w, len, errno);
        return PULSE_ERR;
    }
    return PULSE_OK;
}



