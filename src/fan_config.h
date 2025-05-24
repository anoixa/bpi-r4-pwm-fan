#ifndef FAN_CONFIG_H
#define FAN_CONFIG_H

// PWM sysfs 路径
#define DUTY_PATH "/sys/class/pwm/pwmchip0/pwm1/duty_cycle"
#define PERIOD_PATH "/sys/class/pwm/pwmchip0/pwm1/period"
#define POLARITY_PATH "/sys/class/pwm/pwmchip0/pwm1/polarity"
#define ENABLE_PATH "/sys/class/pwm/pwmchip0/pwm1/enable"
#define EXPORT_PATH "/sys/class/pwm/pwmchip0/export"
#define PWM_FAN_PATH "/sys/class/pwm/pwmchip0/pwm1"

// 温度路径
#define TEMP_PATH "/sys/class/thermal/thermal_zone0/temp"

// 日志
#include <syslog.h>
#define LOG_CUSTOM_INFO(fmt, ...) syslog(LOG_INFO, "fan-speed: " fmt, ##__VA_ARGS__)
#define LOG_CUSTOM_ERROR(fmt, ...) syslog(LOG_ERR, "fan-speed: " fmt, ##__VA_ARGS__)

// 温度
#define TEMP_OFF_MAX     42000
#define TEMP_LOW_MAX     48000
#define TEMP_MID_MAX     58000
#define TEMP_HIGH_MIN    60000
#define HYSTERESIS       2000

// PWM 占空比
#define DUTY_OFF         1000
#define DUTY_LOW         4000
#define DUTY_MID         7000
#define DUTY_HIGH        10000

#endif // FAN_CONFIG_H
