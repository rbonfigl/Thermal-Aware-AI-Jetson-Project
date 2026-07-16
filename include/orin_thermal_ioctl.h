#ifndef ORIN_THERMAL_IOCTL_H
#define ORIN_THERMAL_IOCTL_H

#include <linux/ioctl.h>

struct thermal_telemetry{
    int temperature;
    int fsm_state;
};

#define JETSON_THERMAL_MAGIC 'j'

#define JETSON_THERMAL_RESET _IO(JETSON_THERMAL_MAGIC, 80)
#define JETSON_THERMAL_WRITE _IOW(JETSON_THERMAL_MAGIC,81,int)
#define JETSON_THERMAL_READ _IOR(JETSON_THERMAL_MAGIC,82, struct thermal_telemetry)

#endif