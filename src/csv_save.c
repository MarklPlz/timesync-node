#include <gpiod.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

// Define the GPIO chip and line
// pulldown: sudo raspi-gpio set 17 pd
#define GPIO_CHIP "/dev/gpiochip0"
#define GPIO_LINE 17  // GPIO17

// Define the CSV file path
#define CSV_FILE_PATH "trigger_log.csv"

// Mutex for thread synchronization
pthread_mutex_t fileMutex;

// Function to get the current timestamp
void getTimestamp(char *buffer, size_t bufferSize) {
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, bufferSize, "%Y-%m-%d %H:%M:%S", timeinfo);
}

// Function to write to the CSV file
void writeToCSV() {
    char timestamp[20];
    getTimestamp(timestamp, sizeof(timestamp));

    pthread_mutex_lock(&fileMutex);

    FILE *file = fopen(CSV_FILE_PATH, "a");
    if (file == NULL) {
        perror("Failed to open file");
        pthread_mutex_unlock(&fileMutex);
        return;
    }

    fprintf(file, "%s,Triggered\n", timestamp);
    fclose(file);

    pthread_mutex_unlock(&fileMutex);
}

// Thread function to handle the event
void *eventHandler(void *arg) {
    (void)arg;
    writeToCSV();
    return NULL;
}

int main(void) {
    struct gpiod_chip *chip;
    struct gpiod_line *line;
    struct gpiod_line_event event;
    int ret;

    // Initialize the mutex
    if (pthread_mutex_init(&fileMutex, NULL) != 0) {
        fprintf(stderr, "Mutex init failed\n");
        return 1;
    }

    // Open the GPIO chip
    chip = gpiod_chip_open(GPIO_CHIP);
    if (!chip) {
        perror("Open GPIO chip failed");
        return 1;
    }

    // Get the GPIO line
    line = gpiod_chip_get_line(chip, GPIO_LINE);
    if (!line) {
        perror("Get GPIO line failed");
        gpiod_chip_close(chip);
        return 1;
    }

    // Request the line as an input with event detection
    ret = gpiod_line_request_rising_edge_events(line, "gpio_trigger_logger");
    if (ret < 0) {
        perror("Request GPIO line as input failed");
        gpiod_chip_close(chip);
        return 1;
    }

    // Main loop
    printf("Monitoring GPIO line %d. Press Ctrl+C to exit.\n", GPIO_LINE);
    while (1) {
        ret = gpiod_line_event_wait(line, NULL);
        if (ret < 0) {
            perror("Wait for event failed");
            break;
        }

        ret = gpiod_line_event_read(line, &event);
        if (ret < 0) {
            perror("Read event failed");
            break;
        }

        pthread_t thread;
        pthread_create(&thread, NULL, eventHandler, NULL);
        pthread_detach(thread);
    }

    // Release the line and close the chip
    gpiod_line_release(line);
    gpiod_chip_close(chip);

    // Destroy the mutex
    pthread_mutex_destroy(&fileMutex);

    return 0;
}
