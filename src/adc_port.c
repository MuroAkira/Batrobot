#include "adc_port.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>

struct adc_port {
    int fd;
};

static speed_t baud_to_flag(int baudrate)
{
    switch (baudrate) {
    case 115200: return B115200; /* ここは後で最適化。いまは足場 */
    case 9600:   return B9600;
    default:     return 0;
    }
}

adc_port_t* adc_open(const char* devpath, int baudrate)
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

    adc_port_t* adc = (adc_port_t*)malloc(sizeof(adc_port_t));
    if (!adc) {
        close(fd);
        return NULL;
    }
    adc->fd = fd;
    return adc;
}

void adc_close(adc_port_t* adc)
{
    if (!adc) return;
    if (adc->fd >= 0) close(adc->fd);
    free(adc);
}

adc_result_t adc_flush(adc_port_t* adc)
{
    if (!adc || adc->fd < 0) return ADC_ERR;
    if (tcflush(adc->fd, TCIFLUSH) != 0) return ADC_ERR;
    return ADC_OK;
}

int adc_read(adc_port_t* adc, uint8_t* buf, size_t len, int timeout_ms)
{
    if (!adc || adc->fd < 0 || !buf) return -1;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(adc->fd, &rfds);

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int r = select(adc->fd + 1, &rfds, NULL, NULL, &tv);
    if (r <= 0) {
        return 0; /* timeout or no data */
    }

    ssize_t n = read(adc->fd, buf, len);
    if (n < 0) return -1;
    return (int)n;
}
