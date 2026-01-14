#include "pulse_port.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

struct pulse_port {
    int fd;
};

static speed_t baud_to_flag(int baudrate)
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
