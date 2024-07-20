#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define DEVICE_FILE_NAME "/dev/bme280"
#define IOCTL_GET_TEMPERATURE _IOR('t', 1, long int)
#define IOCTL_GET_HUMIDITY    _IOR('h', 2, long int)
#define IOCTL_GET_PRESSURE    _IOR('p', 3, long int)

int main() {
    int fd;
    long int value;

    fd = open(DEVICE_FILE_NAME, 0);
    if (fd < 0) {
        perror("Failed to open the device");
        return EXIT_FAILURE;
    }

    if (ioctl(fd, IOCTL_GET_TEMPERATURE, &value) < 0) {
        perror("IOCTL_GET_TEMPERATURE failed");
    } else {
        printf("Temperature: %f C\n", value / 100.0);
    }

    if (ioctl(fd, IOCTL_GET_HUMIDITY, &value) < 0) {
        perror("IOCTL_GET_HUMIDITY failed");
    } else {
        printf("Humidity: %f %%\n", value / 1024.0);
    }

    if (ioctl(fd, IOCTL_GET_PRESSURE, &value) < 0) {
        perror("IOCTL_GET_PRESSURE failed");
    } else {
        printf("Pressure: %f Pa\n", value / 256.0);
    }

    close(fd);
    return 0;
}
