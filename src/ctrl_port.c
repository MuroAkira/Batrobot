#include "ctrl_port.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

/* 実体は .c に隠す（opaque） */
struct ctrl_port {
    int fd;
};

/* ボーレート変換 受け取ったbaudrateを通信速度として設定*/
static speed_t baud_to_flag(int baudrate)
{
    switch (baudrate) {
    case 115200: return B115200;
    case 9600:   return B9600;
    default:     return 0;  /* 未対応 */
    }
}

ctrl_port_t* ctrl_open(const char* devpath, int baudrate)
{
    int fd = -1;
    struct termios tio;
    speed_t speed;
    ctrl_port_t* ctrl = NULL;

    if (!devpath) {
        return NULL;
    }

    speed = baud_to_flag(baudrate);
    if (speed == 0) {
        return NULL;
    }

    /* ノンブロッキングで開く（安全） */
    fd = open(devpath, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return NULL;
    }

    /* 現在設定を取得 */
    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return NULL;
    }

    /* 生モード相当（最低限） */
    cfmakeraw(&tio);

    /* ボーレート設定 */
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);

    /* 8N1 */
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;

    /* タイムアウト設定（read未使用だが安全のため） */
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return NULL;
    }

    /* ハンドル確保 */
    ctrl = (ctrl_port_t*)malloc(sizeof(ctrl_port_t));
    if (!ctrl) {
        close(fd);
        return NULL;
    }

    ctrl->fd = fd;
    return ctrl;
}

void ctrl_close(ctrl_port_t* ctrl)
{
    if (!ctrl) {
        return;
    }
    if (ctrl->fd >= 0) {
        close(ctrl->fd);
    }
    free(ctrl);
}

#include <sys/select.h>

ctrl_result_t ctrl_enq(ctrl_port_t* ctrl)
{
    if (!ctrl || ctrl->fd < 0) {
        return CTRL_ERR;
    }

    /* ENQ (0x05) */
    unsigned char enq = 0x05;
    ssize_t w = write(ctrl->fd, &enq, 1);
    if (w != 1) {
        return CTRL_ERR;
    }

    /* ACK (0x06) を最大 500ms 待つ */
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(ctrl->fd, &rfds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500 * 1000;

    int r = select(ctrl->fd + 1, &rfds, NULL, NULL, &tv);
    if (r <= 0) {
        return CTRL_ERR; /* timeout or error */
    }

    unsigned char resp = 0;
    ssize_t n = read(ctrl->fd, &resp, 1);
    if (n == 1 && resp == 0x06) {
        return CTRL_OK;
    }

    return CTRL_ERR;
}

#include <stdio.h>

/* 1行読む（\nまで）。最大 maxlen-1 文字で終端\0を付ける */
static int read_line(int fd, char* buf, size_t maxlen, int timeout_ms)
{
    size_t pos = 0;
    while (pos + 1 < maxlen) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int r = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (r <= 0) break; /* timeout or error */

        unsigned char c;
        ssize_t n = read(fd, &c, 1);
        if (n != 1) break;

        buf[pos++] = (char)c;
        if (c == '\n') break;
    }
    buf[pos] = '\0';
    return (int)pos;
}

ctrl_result_t ctrl_get_sampling_hz(ctrl_port_t* ctrl, uint32_t* hz_out)
{
    if (!ctrl || ctrl->fd < 0 || !hz_out) return CTRL_ERR;

    /* 受信バッファを軽く掃除（残りゴミ対策） */
    tcflush(ctrl->fd, TCIFLUSH);

    /* f + 改行 を送る */
    const char* cmd = "f\n";
    if (write(ctrl->fd, cmd, 2) != 2) return CTRL_ERR;

    /* 返答1行を読む（例: "1000000\r\n"） */
    char line[64];
    int n = read_line(ctrl->fd, line, sizeof(line), 500);
    if (n <= 0) return CTRL_ERR;

    /* 数値に変換 */
    unsigned long v = strtoul(line, NULL, 10);
    if (v == 0) return CTRL_ERR;

    *hz_out = (uint32_t)v;
    return CTRL_OK;
}

