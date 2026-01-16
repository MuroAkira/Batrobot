#include "ctrl_port.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <stdio.h>

/* 実体は .c に隠す（opaque） */
struct ctrl_port {
    int fd;
    char devpath[256];
};

/* ボーレート変換 受け取ったbaudrateを通信速度として設定 */
static speed_t baud_to_flag(int baudrate)
{
    switch (baudrate) {
    case 115200: return B115200;
    default:     return 0;  /* 未対応 */
    }
}

ctrl_port_t* ctrl_open(const char* devpath, int baudrate)
{
    if (!devpath) return NULL;

    speed_t sp = baud_to_flag(baudrate);
    if (sp == 0) return NULL;

    /* PortC: コマンド送受信用なのでノンブロッキングにしない方が扱いやすい */
    int fd = open(devpath, O_RDWR | O_NOCTTY);
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

    /* 受信待ちは select() で制御するので 0/0 でOK */
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return NULL;
    }

    ctrl_port_t* c = (ctrl_port_t*)malloc(sizeof(ctrl_port_t));
    if (!c) {
        close(fd);
        return NULL;
    }
    c->fd = fd;
    snprintf(c->devpath, sizeof(c->devpath), "%s", devpath);
    return c;
}

void ctrl_close(ctrl_port_t* ctrl)
{
    if (!ctrl) return;
    if (ctrl->fd >= 0) close(ctrl->fd);
    free(ctrl);
}

/* 文字列コマンドを送る（例: "g 300\n", "e\n", "?\n"） */
ctrl_result_t ctrl_send_line(ctrl_port_t* ctrl, const char* line)
{
    if (!ctrl || ctrl->fd < 0 || !line) return CTRL_ERR;
    size_t len = strlen(line);
    if (len == 0) return CTRL_ERR;

    ssize_t w = write(ctrl->fd, line, len);
    return (w == (ssize_t)len) ? CTRL_OK : CTRL_ERR;
}

ctrl_result_t ctrl_enq(ctrl_port_t* ctrl)
{
    if (!ctrl || ctrl->fd < 0) return CTRL_ERR;

    /* ENQ (0x05) */
    unsigned char enq = 0x05;
    ssize_t w = write(ctrl->fd, &enq, 1);
    if (w != 1) return CTRL_ERR;

    /* ACK (0x06) を最大 500ms 待つ */
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(ctrl->fd, &rfds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500 * 1000;

    int r = select(ctrl->fd + 1, &rfds, NULL, NULL, &tv);
    if (r <= 0) return CTRL_ERR; /* timeout or error */

    unsigned char resp = 0;
    ssize_t n = read(ctrl->fd, &resp, 1);
    if (n == 1 && resp == 0x06) return CTRL_OK;

    return CTRL_ERR;
}

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

/* e コマンドでエラー回数を取得する（仕様: "pulse_error adc_error\r\n"） */
ctrl_result_t ctrl_get_errors(ctrl_port_t* ctrl, uint32_t* pulse_err, uint32_t* adc_err)
{
    if (!ctrl || ctrl->fd < 0 || !pulse_err || !adc_err) return CTRL_ERR;

    tcflush(ctrl->fd, TCIFLUSH);

    const char* cmd = "e\n";
    if (write(ctrl->fd, cmd, 2) != 2) return CTRL_ERR;

    char line[128];
    int n = read_line(ctrl->fd, line, sizeof(line), 500);
    if (n <= 0) return CTRL_ERR;

    /* 例: "0 0\r\n" */
    unsigned long pe = 0, ae = 0;
    if (sscanf(line, "%lu %lu", &pe, &ae) != 2) return CTRL_ERR;

    *pulse_err = (uint32_t)pe;
    *adc_err   = (uint32_t)ae;
    return CTRL_OK;
}
