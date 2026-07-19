#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include "orin_thermal_ioctl.h"

int main() {
    int fd = open("/dev/JETSON", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct thermal_telemetry *mapped = mmap(
        NULL,                          // let kernel choose address
        sizeof(struct thermal_telemetry),
        PROT_READ,                     // read-only mapping
        MAP_SHARED,
        fd,
        0                               // offset
    );

    if (mapped == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    printf("Temperature: %d, FSM State: %d\n", mapped->temperature, mapped->fsm_state);

    printf("Reading again directly from mapped memory: Temperature: %d\n", mapped->temperature);

    munmap(mapped, sizeof(struct thermal_telemetry));
    close(fd);
    return 0;
}