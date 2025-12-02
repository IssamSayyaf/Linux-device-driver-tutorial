/*
 * gps_test.c - Userspace test application for GNSS driver
 *
 * This application demonstrates:
 * - Reading NMEA data from GNSS character device
 * - Parsing NMEA sentences
 * - Displaying GPS position data
 *
 * Build:
 *   gcc -o gps_test gps_test.c
 *
 * Usage:
 *   ./gps_test [gnss_device]
 *   ./gps_test /dev/gnss0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

#define DEFAULT_DEVICE  "/dev/gnss0"
#define BUFFER_SIZE     1024
#define MAX_NMEA_LEN    82

/* GPS data structure */
struct gps_data {
    /* Position */
    double latitude;
    double longitude;
    double altitude;
    char lat_dir;       /* N/S */
    char lon_dir;       /* E/W */

    /* Time */
    int hour;
    int minute;
    int second;

    /* Date */
    int day;
    int month;
    int year;

    /* Quality */
    int fix_quality;    /* 0=invalid, 1=GPS, 2=DGPS */
    int satellites;
    float hdop;

    /* Speed and course */
    float speed_knots;
    float course;

    /* Status */
    char status;        /* A=active, V=void */
    int valid;
};

/* Calculate NMEA checksum */
static int nmea_checksum(const char *sentence)
{
    int checksum = 0;
    const char *p;

    if (sentence[0] == '$')
        sentence++;

    for (p = sentence; *p && *p != '*'; p++)
        checksum ^= *p;

    return checksum;
}

/* Verify NMEA checksum */
static int nmea_verify(const char *sentence)
{
    char *asterisk;
    int calc_checksum, recv_checksum;

    asterisk = strchr(sentence, '*');
    if (!asterisk)
        return 0;

    calc_checksum = nmea_checksum(sentence);
    recv_checksum = strtol(asterisk + 1, NULL, 16);

    return (calc_checksum == recv_checksum);
}

/* Convert NMEA coordinate to decimal degrees */
static double nmea_to_decimal(double nmea_coord, char direction)
{
    int degrees;
    double minutes, decimal;

    degrees = (int)(nmea_coord / 100);
    minutes = nmea_coord - (degrees * 100);
    decimal = degrees + (minutes / 60.0);

    if (direction == 'S' || direction == 'W')
        decimal = -decimal;

    return decimal;
}

/* Parse GPGGA sentence (Fix Data) */
static int parse_gpgga(const char *sentence, struct gps_data *gps)
{
    char time_str[16];
    double lat, lon;
    char lat_dir, lon_dir;
    int quality, sats;
    float hdop, alt;
    char alt_unit;

    int ret = sscanf(sentence,
                     "$G%*cGGA,%15[^,],%lf,%c,%lf,%c,%d,%d,%f,%f,%c",
                     time_str, &lat, &lat_dir, &lon, &lon_dir,
                     &quality, &sats, &hdop, &alt, &alt_unit);

    if (ret >= 10) {
        /* Parse time */
        if (strlen(time_str) >= 6) {
            gps->hour = (time_str[0] - '0') * 10 + (time_str[1] - '0');
            gps->minute = (time_str[2] - '0') * 10 + (time_str[3] - '0');
            gps->second = (time_str[4] - '0') * 10 + (time_str[5] - '0');
        }

        gps->latitude = nmea_to_decimal(lat, lat_dir);
        gps->lat_dir = lat_dir;
        gps->longitude = nmea_to_decimal(lon, lon_dir);
        gps->lon_dir = lon_dir;
        gps->fix_quality = quality;
        gps->satellites = sats;
        gps->hdop = hdop;
        gps->altitude = alt;

        if (quality > 0)
            gps->valid = 1;

        return 1;
    }

    return 0;
}

/* Parse GPRMC sentence (Recommended Minimum) */
static int parse_gprmc(const char *sentence, struct gps_data *gps)
{
    char time_str[16], status;
    double lat, lon;
    char lat_dir, lon_dir;
    float speed, course;
    char date_str[16];

    int ret = sscanf(sentence,
                     "$G%*cRMC,%15[^,],%c,%lf,%c,%lf,%c,%f,%f,%15[^,]",
                     time_str, &status, &lat, &lat_dir, &lon, &lon_dir,
                     &speed, &course, date_str);

    if (ret >= 9) {
        gps->status = status;

        if (status == 'A') {
            gps->latitude = nmea_to_decimal(lat, lat_dir);
            gps->lat_dir = lat_dir;
            gps->longitude = nmea_to_decimal(lon, lon_dir);
            gps->lon_dir = lon_dir;
            gps->speed_knots = speed;
            gps->course = course;
            gps->valid = 1;

            /* Parse date DDMMYY */
            if (strlen(date_str) >= 6) {
                gps->day = (date_str[0] - '0') * 10 + (date_str[1] - '0');
                gps->month = (date_str[2] - '0') * 10 + (date_str[3] - '0');
                gps->year = 2000 + (date_str[4] - '0') * 10 + (date_str[5] - '0');
            }
        }

        return 1;
    }

    return 0;
}

/* Parse NMEA sentence */
static int parse_nmea(const char *sentence, struct gps_data *gps)
{
    /* Verify checksum */
    if (!nmea_verify(sentence)) {
        return 0;
    }

    /* Parse based on sentence type */
    if (strstr(sentence, "GGA"))
        return parse_gpgga(sentence, gps);
    else if (strstr(sentence, "RMC"))
        return parse_gprmc(sentence, gps);

    return 0;
}

/* Display GPS data */
static void display_gps_data(const struct gps_data *gps)
{
    printf("\033[H\033[J");  /* Clear screen */

    printf("=== GPS Data ===\n\n");

    if (gps->valid) {
        printf("Status: %s\n", (gps->fix_quality > 0) ? "FIX" : "NO FIX");
        printf("Fix Quality: %d (%s)\n", gps->fix_quality,
               gps->fix_quality == 0 ? "Invalid" :
               gps->fix_quality == 1 ? "GPS" :
               gps->fix_quality == 2 ? "DGPS" : "Other");
        printf("Satellites: %d\n", gps->satellites);
        printf("HDOP: %.2f\n\n", gps->hdop);

        printf("Position:\n");
        printf("  Latitude:  %.6f° %c\n", gps->latitude > 0 ? gps->latitude : -gps->latitude,
               gps->latitude >= 0 ? 'N' : 'S');
        printf("  Longitude: %.6f° %c\n", gps->longitude > 0 ? gps->longitude : -gps->longitude,
               gps->longitude >= 0 ? 'E' : 'W');
        printf("  Altitude:  %.1f m\n\n", gps->altitude);

        printf("Motion:\n");
        printf("  Speed:  %.1f knots (%.1f km/h)\n",
               gps->speed_knots, gps->speed_knots * 1.852);
        printf("  Course: %.1f°\n\n", gps->course);

        printf("Time: %02d:%02d:%02d UTC\n", gps->hour, gps->minute, gps->second);
        printf("Date: %02d/%02d/%04d\n", gps->day, gps->month, gps->year);
    } else {
        printf("Waiting for GPS fix...\n");
        printf("Status: %c\n", gps->status);
    }
}

int main(int argc, char *argv[])
{
    const char *device = DEFAULT_DEVICE;
    int fd;
    char buffer[BUFFER_SIZE];
    char line[MAX_NMEA_LEN + 1];
    int line_pos = 0;
    struct gps_data gps = {0};
    ssize_t n;

    /* Parse arguments */
    if (argc > 1)
        device = argv[1];

    printf("Opening GPS device: %s\n", device);

    /* Open GNSS device */
    fd = open(device, O_RDONLY | O_NOCTTY);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    printf("Reading NMEA data (Ctrl+C to stop)...\n\n");

    /* Read and parse NMEA data */
    while ((n = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';

        /* Process each character */
        for (int i = 0; i < n; i++) {
            char c = buffer[i];

            if (c == '$') {
                /* Start of new sentence */
                line_pos = 0;
                line[line_pos++] = c;
            } else if (c == '\n' || c == '\r') {
                /* End of sentence */
                if (line_pos > 0) {
                    line[line_pos] = '\0';

                    /* Parse and update display */
                    if (parse_nmea(line, &gps)) {
                        display_gps_data(&gps);
                    }

                    line_pos = 0;
                }
            } else if (line_pos < MAX_NMEA_LEN) {
                /* Add to current line */
                line[line_pos++] = c;
            }
        }
    }

    if (n < 0)
        perror("Read error");

    close(fd);
    return 0;
}
