#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "orin_thermal_ioctl.h"

int main() {
    int fd = open("/dev/JETSON", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    
    struct thermal_telemetry data;

    // Test READ for GPU
    if (ioctl(fd, JETSON_THERMAL_GPU_READ, &data) < 0) {
        perror("ioctl READ");
        close(fd);
        return 1;
    }
    printf("GPU: Temperature: %d, FSM State: %d\n", data.temperature, data.fsm_state);

    // Test READ for CPU
    if (ioctl(fd, JETSON_THERMAL_CPU_READ, &data) < 0) {
        perror("ioctl READ");
        close(fd);
        return 1;
    }
    printf("CPU: Temperature: %d, FSM State: %d\n", data.temperature, data.fsm_state);

    // Test WRITE
    int new_value = 99;
    if (ioctl(fd, JETSON_THERMAL_WRITE, &new_value) < 0) {
        perror("ioctl WRITE");
        close(fd);
        return 1;
    }
    printf("Write command sent successfully\n");

    // Test RESET
    if (ioctl(fd, JETSON_THERMAL_RESET) < 0) {
        perror("ioctl RESET");
        close(fd);
        return 1;
    }
    printf("Reset command sent successfully\n");

    close(fd);
    return 0;
}