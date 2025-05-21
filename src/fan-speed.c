#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <syslog.h>
#include <signal.h>

#define DUTY_PATH "/sys/class/pwm/pwmchip0/pwm1/duty_cycle"
#define PERIOD_PATH "/sys/class/pwm/pwmchip0/pwm1/period"
#define POLARITY_PATH "/sys/class/pwm/pwmchip0/pwm1/polarity"
#define ENABLE_PATH "/sys/class/pwm/pwmchip0/pwm1/enable"
#define EXPORT_PATH "/sys/class/pwm/pwmchip0/export"
#define TEMP_PATH "/sys/class/thermal/thermal_zone0/temp"

#define LOG(fmt, ...) syslog(LOG_INFO, "fan-speed: " fmt, ##__VA_ARGS__)

#define TEMP_HIGH 60000   // 60°C
#define TEMP_MID 50000    // 50°C
#define TEMP_LOW 40000    // 40°C
#define HYSTERESIS 2000   // 2°C

volatile sig_atomic_t running = 1;

void handle_signal(int sig) {
    running = 0;
}

int write_file(const char *path, const char *val) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    int r = write(fd, val, strlen(val));
    close(fd);
    return r == strlen(val) ? 0 : -1;
}

void set_duty_cycle(int duty, int *last_duty) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", duty);
    if (write_file(DUTY_PATH, buf) == 0) {
        *last_duty = duty;
        LOG("Temp adjusted: duty=%d", duty);
    } else {
        LOG("Failed to set duty=%d", duty);
    }
}

int main() {
    openlog("fan-speed", LOG_PID, LOG_USER);
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    int last_duty = -1;

    if (access("/sys/class/pwm/pwmchip0/pwm1", F_OK) != 0) {
        if (write_file(EXPORT_PATH, "1") < 0) {
            LOG("Export failed!");
            closelog();
            return -1;
        }
        sleep(1);
    }

    if (write_file(PERIOD_PATH, "10000") < 0 ||
        write_file(POLARITY_PATH, "normal") < 0 ||
        write_file(ENABLE_PATH, "1") < 0) {
        LOG("PWM init failed!");
        closelog();
        return -1;
    }

    set_duty_cycle(7000, &last_duty);
    LOG("Service started");

    int tick = 0;
    while (running) {
        sleep(20);
        tick++;

        // 读取温度
        FILE *fp = fopen(TEMP_PATH, "r");
        if (!fp) {
            LOG("Temp read failed");
            continue;
        }

        int temp = 0;
        if (fscanf(fp, "%d", &temp) != 1) {
            LOG("Temp parse failed");
            fclose(fp);
            continue;
        }
        fclose(fp);

        // 计算新占空比
        int new_duty = last_duty;
        if (temp >= TEMP_HIGH + HYSTERESIS) {
            new_duty = 1000;
        } else if (temp >= TEMP_MID + HYSTERESIS) {
            new_duty = 5000;
        } else if (temp <= TEMP_LOW - HYSTERESIS) {
            new_duty = 10000;
        } else if (temp <= TEMP_MID - HYSTERESIS) {
            new_duty = 7000;
        }

        if (new_duty != last_duty) {
            set_duty_cycle(new_duty, &last_duty);
        }

        if (tick >= 180) {
            tick = 0;
            LOG("Status: temp=%d°C, duty=%d", temp/1000, last_duty);
        }
    }

    write_file(ENABLE_PATH, "0");
    LOG("Service stopped");
    closelog();
    return 0;
}