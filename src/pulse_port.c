#include "pulse_port.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

struct pulse_port {
    int fd;
};// pulse_port_tは整数型のファイルディスクリプタを持つ構造体

static speed_t baud_to_flag(int baudrate)// ボーレートを対応するtermiosの速度フラグに変換する関数
{
    switch (baudrate) {
    case 115200: return B115200;
    default:     return 0;
    }
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
    return p;
}

void pulse_close(pulse_port_t* p)
{
    if (!p) return;
    if (p->fd >= 0) close(p->fd);
    free(p);
}

pulse_result_t pulse_write_locked(pulse_port_t* p, const uint8_t* data, size_t len)
{
    (void)p; (void)data; (void)len;

    /*
     * 安全ロック：この関数は常に送信拒否。
     * 音が出る可能性があるため、あなたの許可なしに実行される経路を物理的に潰す。
     */
    return PULSE_ERR;
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

