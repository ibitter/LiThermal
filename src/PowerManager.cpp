// C library headers
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
// Linux headers
#include <fcntl.h>   // Contains file controls like O_RDWR
#include <errno.h>   // Error integer and strerror() function
#include <termios.h> // Contains POSIX terminal control definitions
#include <unistd.h>  // write(), read(), close()
#include <sys/select.h>

static int serial_fd = 0;
int serialSetup()
{
    int serial_port;
    serial_port = open("/dev/ttyS3", O_RDWR | O_NOCTTY);
    if (serial_port < 0)
    {
        perror("open uart device error\n");
        return -1;
    }
    // Create new termios struct, we call it 'tty' for convention
    // No need for "= {0}" at the end as we'll immediately write the existing
    // config to this struct
    struct termios tty;

    // Read in existing settings, and handle any error
    // NOTE: This is important! POSIX states that the struct passed to tcsetattr()
    // must have been initialized with a call to tcgetattr() overwise behaviour
    // is undefined
    if (tcgetattr(serial_port, &tty) != 0)
    {
        printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
        return -1;
    }
    tty.c_iflag &= ~(IGNBRK | BRKINT | ICRNL |
        INLCR | PARMRK | INPCK | ISTRIP | IXON);


    tty.c_oflag = 0;
    //
    // No line processing
    //
    // echo off, echo newline off, canonical mode off,
    // extended input processing off, signal chars off
    //
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);

    //
    // Turn off character processing
    //
    // clear current char size mask, no parity checking,
    // no output processing, force 8 bit input
    //
    tty.c_cflag &= ~(CSIZE | PARENB);
    tty.c_cflag |= CS8;

    //
    // One input byte is enough to return from read()
    // Inter-character timer off
    //
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    if (tcsetattr(serial_port, TCSAFLUSH, &tty) != 0)
    {
        printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
        return -1;
    }
    tcflush(serial_port, TCIOFLUSH); // Flush serial buffer
    return serial_port;
}

static int serialWrite(int fd, uint8_t command)
{
    int len;
    len = write(fd, &command, 1);
    if (len < 0)
        return 0;
    return 1;
}

#define SERIAL_CMD_READ_ADC 0x58
#define SERIAL_CMD_POWEROFF 0x6a
#define SERIAL_CMD_USBMODE_NORMAL 0x11
#define SERIAL_CMD_USBMODE_WIFI_CAM 0x12
#define SERIAL_CMD_USBMODE_DIRECT 0x13
#define SERIAL_CMD_IS_CHARGING 0x59

// 滤波参数（可根据你的精度调整）
#define BAT_FILTER_COUNT        5       // 连续采样5次
#define BAT_MAX_DELTA_VOLTAGE   50      // 最大允许波动 50mV
#define BAT_INVALID_VALUE       -1

// 全局静态变量（保存历史值，用于平滑）
static int16_t g_last_voltage = 0;

// 稳定版：读取电池电压（带滤波）
int16_t PowerManager_getBatteryVoltage(void)
{
    int16_t adc_buf[BAT_FILTER_COUNT] = {0};
    int32_t adc_sum = 0;
    uint8_t valid_cnt = 0;

    // ====================== 1. 连续采样 N 次 ======================
    for (int i = 0; i < BAT_FILTER_COUNT; i++)
    {
        // 调用原始单次读取函数
        int16_t val = __battery_read_raw_adc();

        // 失败则重试
        if (val == BAT_INVALID_VALUE)
        {
            i--;
            continue;
        }

        adc_buf[i] = val;
        adc_sum += val;
        valid_cnt++;
    }

    // ====================== 2. 求平均值 ======================
    int32_t avg_adc = adc_sum / valid_cnt;

    // ====================== 3. 限幅滤波（防止跳变） ======================
    int32_t delta = abs(avg_adc - g_last_voltage);
    if (g_last_voltage != 0 && delta > BAT_MAX_DELTA_VOLTAGE)
    {
        // 波动太大 → 不更新，返回上一次稳定值
        return g_last_voltage;
    }

    // ====================== 4. 保存并返回 ======================
    g_last_voltage = (int16_t)avg_adc;
    return g_last_voltage;
}

// 原始底层：只负责读一次串口（不对外使用）
static int16_t __battery_read_raw_adc(void)
{
    char buf[2];
    int len = -1;
    int16_t result = 0;

    // 发读命令
    serialWrite(serial_fd, SERIAL_CMD_READ_ADC);
    tcflush(serial_fd, TCIOFLUSH);

    // select 超时等待
    fd_set set;
    struct timeval timeout;
    FD_ZERO(&set);
    FD_SET(serial_fd, &set);

    timeout.tv_sec = 0;
    timeout.tv_usec = 80000; // 80ms 超时

    int select_ret = select(serial_fd + 1, &set, NULL, NULL, &timeout);
    if (select_ret <= 0)
        return BAT_INVALID_VALUE;

    // 读取2字节
    len = read(serial_fd, buf, 2);
    if (len != 2)
        return BAT_INVALID_VALUE;

    // 拼接16位
    result = ((uint8_t)buf[0] << 8) | (uint8_t)buf[1];
    return result;
}

bool PowerManager_isCharging()
{
    char buf = 0;
    serialWrite(serial_fd, SERIAL_CMD_IS_CHARGING);
    // tcflush(serial_fd, TCIOFLUSH); // Flush serial buffer
    /*author: https://stackoverflow.com/a/2918709 */
    fd_set set;
    struct timeval timeout;
    FD_ZERO(&set);           /* clear the set */
    FD_SET(serial_fd, &set); /* add our file descriptor to the set */
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    int select_result = select(serial_fd + 1, &set, NULL, NULL, &timeout);
    if (select_result == -1)
        return false; /* an error accured */
    else if (select_result == 0)
        return false; /* a timeout occured */
    else
        read(serial_fd, &buf, 1);

    return (buf == 1);
}

void PowerManager_init()
{
    serial_fd = serialSetup();
}

#include <signal.h>
void PowerManager_powerOff()
{
    if (serial_fd <= 0)
        return;
    serialWrite(serial_fd, SERIAL_CMD_POWEROFF);
    system("echo 1 > /tmp/poweroff");
    system("poweroff");
    // stop here
    exit(0);
}
