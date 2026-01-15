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
<<<<<<< HEAD
    char devpath[256];
};
=======
};// pulse_port_tは整数型のファイルディスクリプタを持つ構造体
>>>>>>> b1b2b1e76b49f3453593d3d4dd312e0eecb2269c

static speed_t baud_to_flag(int baudrate)// ボーレートを対応するtermiosの速度フラグに変換する関数
{
    switch (baudrate) {
    case 115200: return B115200;
    default:     return 0;
    }
}

static int is_safe_devpath(const char* p)
{
    /* “偽ポートだけ許可” ルール。ここが最重要。 */
    return (p && strncmp(p, "/tmp/PULSE_", 11) == 0);
}

pulse_port_t* pulse_open(const char* devpath, int baudrate)
{
    if (!devpath) return NULL;

    speed_t sp = baud_to_flag(baudrate);
    if (sp == 0) return NULL;

    int fd = open(devpath, O_RDWR | O_NOCTTY | O_NONBLOCK);
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
    if (duty_percent < 1 || duty_percent > 99) return 0;

    /* 10MHz基準 → 1周期のtick数は 10000/freq_khz （0.1us単位） */
    int period_ticks = (10000 + freq_khz/2) / freq_khz;   /* 四捨五入 */
    if (period_ticks < 1) period_ticks = 1;

    int on_ticks = (period_ticks * duty_percent + 50) / 100;
    if (on_ticks < 1) on_ticks = 1;
    if (on_ticks >= period_ticks) on_ticks = period_ticks - 1;

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

pulse_result_t pulse_write_locked(pulse_port_t* p, const uint8_t* data, size_t len)
{
    if (!p || p->fd < 0 || !data || len == 0) return PULSE_ERR;

    /* 実機へは絶対に流さない */
    if (!is_safe_devpath(p->devpath)) {
        fprintf(stderr, "PULSE locked: devpath=%s\n", p->devpath);
        return PULSE_ERR;
    }

    /* “怖い”対策：0xFF（8/8 High）を拒否 */
    for (size_t i = 0; i < len; i++) {
        if (data[i] == 0xFF) {
            fprintf(stderr, "PULSE blocked: found 0xFF at %zu\n", i);
            return PULSE_ERR;
        }
    }

    ssize_t w = write(p->fd, data, len);
    return (w == (ssize_t)len) ? PULSE_OK : PULSE_ERR;
}

#include <string.h>
#include <stdlib.h>

/* data内の1ビット数を数える（dutyチェック用） */
static unsigned long count_ones(const uint8_t* data, size_t len)
{
    unsigned long c = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t v = data[i];
        for (int b = 0; b < 8; b++) c += (v >> b) & 1u;
    }
    return c;
}

pulse_result_t pulse_write(pulse_port_t* p, const uint8_t* data, size_t len)
{
    if (!p || p->fd < 0 || !data || len == 0) return PULSE_ERR;

    /* === 実行時アーミング === */
    const char* arm = getenv("THERMOPHONE_ARM");
    if (!arm || strcmp(arm, "YES") != 0) {
        /* アーミングされてなければ絶対に送らない */
        return PULSE_ERR;
    }

    /* === コンパイル時アーミング === */
#ifndef ENABLE_SOUND
    return PULSE_ERR;
#endif

    /* === 安全: 送信長（duration）上限 === */
    if (len > 512) { /* まずは極小に固定（後で段階的に増やす） */
        return PULSE_ERR;
    }

    /* === 安全: duty上限 1%（ビット比率） === */
    unsigned long ones = count_ones(data, len);
    unsigned long bits = (unsigned long)len * 8ul;

    /* duty(%) = ones*100/bits。1%を超えたら拒否 */
    if (ones * 100ul > bits * 1ul) {
        return PULSE_ERR;
    }

    /* ここまで来たら送信 */
    ssize_t w = write(p->fd, data, len);
    return (w == (ssize_t)len) ? PULSE_OK : PULSE_ERR;
}

