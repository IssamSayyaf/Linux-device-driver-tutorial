/*
 * mpu6050_test.c - Userspace test application for MPU6050 IIO driver
 *
 * This application demonstrates:
 * - Reading IIO sensor data via sysfs
 * - Reading buffered data via /dev/iio:deviceX
 * - Configuring sample rate and scale
 *
 * Build:
 *   gcc -o mpu6050_test mpu6050_test.c -lm
 *
 * Usage:
 *   ./mpu6050_test [iio_device_number]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <sys/ioctl.h>
#include <linux/iio/buffer.h>

#define IIO_SYSFS_BASE  "/sys/bus/iio/devices/iio:device"
#define DEFAULT_DEVICE  0

/* Helper to read sysfs attribute */
static int read_sysfs_int(const char *path, int *value)
{
    FILE *f;
    int ret;

    f = fopen(path, "r");
    if (!f)
        return -errno;

    ret = fscanf(f, "%d", value);
    fclose(f);

    return (ret == 1) ? 0 : -EINVAL;
}

/* Helper to read sysfs float */
static int read_sysfs_float(const char *path, float *value)
{
    FILE *f;
    int ret;

    f = fopen(path, "r");
    if (!f)
        return -errno;

    ret = fscanf(f, "%f", value);
    fclose(f);

    return (ret == 1) ? 0 : -EINVAL;
}

/* Helper to write sysfs attribute */
static int write_sysfs_int(const char *path, int value)
{
    FILE *f;
    int ret;

    f = fopen(path, "w");
    if (!f)
        return -errno;

    ret = fprintf(f, "%d", value);
    fclose(f);

    return (ret > 0) ? 0 : -EINVAL;
}

/* Read accelerometer data */
static int read_accelerometer(const char *base_path, float *x, float *y, float *z)
{
    char path[256];
    int raw;
    float scale;

    /* Read scale */
    snprintf(path, sizeof(path), "%s/in_accel_scale", base_path);
    if (read_sysfs_float(path, &scale) < 0) {
        fprintf(stderr, "Failed to read accel scale\n");
        return -1;
    }

    /* Read X axis */
    snprintf(path, sizeof(path), "%s/in_accel_x_raw", base_path);
    if (read_sysfs_int(path, &raw) < 0) {
        fprintf(stderr, "Failed to read accel X\n");
        return -1;
    }
    *x = raw * scale;

    /* Read Y axis */
    snprintf(path, sizeof(path), "%s/in_accel_y_raw", base_path);
    if (read_sysfs_int(path, &raw) < 0) {
        fprintf(stderr, "Failed to read accel Y\n");
        return -1;
    }
    *y = raw * scale;

    /* Read Z axis */
    snprintf(path, sizeof(path), "%s/in_accel_z_raw", base_path);
    if (read_sysfs_int(path, &raw) < 0) {
        fprintf(stderr, "Failed to read accel Z\n");
        return -1;
    }
    *z = raw * scale;

    return 0;
}

/* Read gyroscope data */
static int read_gyroscope(const char *base_path, float *x, float *y, float *z)
{
    char path[256];
    int raw;
    float scale;

    /* Read scale */
    snprintf(path, sizeof(path), "%s/in_anglvel_scale", base_path);
    if (read_sysfs_float(path, &scale) < 0) {
        fprintf(stderr, "Failed to read gyro scale\n");
        return -1;
    }

    /* Read X axis */
    snprintf(path, sizeof(path), "%s/in_anglvel_x_raw", base_path);
    if (read_sysfs_int(path, &raw) < 0) {
        fprintf(stderr, "Failed to read gyro X\n");
        return -1;
    }
    *x = raw * scale;

    /* Read Y axis */
    snprintf(path, sizeof(path), "%s/in_anglvel_y_raw", base_path);
    if (read_sysfs_int(path, &raw) < 0) {
        fprintf(stderr, "Failed to read gyro Y\n");
        return -1;
    }
    *y = raw * scale;

    /* Read Z axis */
    snprintf(path, sizeof(path), "%s/in_anglvel_z_raw", base_path);
    if (read_sysfs_int(path, &raw) < 0) {
        fprintf(stderr, "Failed to read gyro Z\n");
        return -1;
    }
    *z = raw * scale;

    return 0;
}

/* Read temperature */
static int read_temperature(const char *base_path, float *temp)
{
    char path[256];
    int raw;
    float scale, offset;

    /* Read scale */
    snprintf(path, sizeof(path), "%s/in_temp_scale", base_path);
    if (read_sysfs_float(path, &scale) < 0) {
        fprintf(stderr, "Failed to read temp scale\n");
        return -1;
    }

    /* Read offset */
    snprintf(path, sizeof(path), "%s/in_temp_offset", base_path);
    if (read_sysfs_float(path, &offset) < 0) {
        offset = 0;  /* Optional */
    }

    /* Read raw */
    snprintf(path, sizeof(path), "%s/in_temp_raw", base_path);
    if (read_sysfs_int(path, &raw) < 0) {
        fprintf(stderr, "Failed to read temp raw\n");
        return -1;
    }

    *temp = (raw + offset) * scale;
    return 0;
}

/* Calculate roll and pitch from accelerometer */
static void calculate_orientation(float ax, float ay, float az,
                                  float *roll, float *pitch)
{
    *roll = atan2(ay, az) * 180.0 / M_PI;
    *pitch = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / M_PI;
}

int main(int argc, char *argv[])
{
    char base_path[128];
    char path[256];
    int device_num = DEFAULT_DEVICE;
    int sample_rate;
    float ax, ay, az;
    float gx, gy, gz;
    float temp;
    float roll, pitch;
    int i;

    /* Parse arguments */
    if (argc > 1)
        device_num = atoi(argv[1]);

    snprintf(base_path, sizeof(base_path), "%s%d", IIO_SYSFS_BASE, device_num);

    /* Check if device exists */
    snprintf(path, sizeof(path), "%s/name", base_path);
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Device %s not found\n", base_path);
        fprintf(stderr, "Usage: %s [device_number]\n", argv[0]);
        return 1;
    }
    char name[64];
    if (fscanf(f, "%63s", name) == 1)
        printf("Device: %s\n", name);
    fclose(f);

    /* Read current sample rate */
    snprintf(path, sizeof(path), "%s/in_accel_sampling_frequency", base_path);
    if (read_sysfs_int(path, &sample_rate) == 0)
        printf("Sample rate: %d Hz\n", sample_rate);

    printf("\nReading sensor data (Ctrl+C to stop)...\n\n");
    printf("%-10s %-10s %-10s | %-10s %-10s %-10s | %-8s | %-8s %-8s\n",
           "Accel X", "Accel Y", "Accel Z",
           "Gyro X", "Gyro Y", "Gyro Z",
           "Temp", "Roll", "Pitch");
    printf("%-10s %-10s %-10s | %-10s %-10s %-10s | %-8s | %-8s %-8s\n",
           "(m/s²)", "(m/s²)", "(m/s²)",
           "(rad/s)", "(rad/s)", "(rad/s)",
           "(°C)", "(°)", "(°)");
    printf("--------------------------------------------------------------------------------\n");

    /* Continuous reading loop */
    for (i = 0; ; i++) {
        /* Read all sensors */
        if (read_accelerometer(base_path, &ax, &ay, &az) < 0)
            break;

        if (read_gyroscope(base_path, &gx, &gy, &gz) < 0)
            break;

        if (read_temperature(base_path, &temp) < 0)
            temp = 0;

        /* Calculate orientation */
        calculate_orientation(ax, ay, az, &roll, &pitch);

        /* Print data */
        printf("\r%10.3f %10.3f %10.3f | %10.4f %10.4f %10.4f | %8.2f | %8.2f %8.2f",
               ax, ay, az, gx, gy, gz, temp, roll, pitch);
        fflush(stdout);

        /* Delay between readings */
        usleep(100000);  /* 100ms = 10 Hz */
    }

    printf("\n");
    return 0;
}
