#include <arpa/inet.h>
#include <gpiod.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MULTICAST_GROUP "224.0.0.1"
#define PORT 12345
#define BUFFSIZE 12
#define GPIO_CHIP "gpiochip0"
#define GPIO_LINE 17
#define CSV_FILE "timestamps_3.csv"

int sockfd;
struct in_addr mreq;

// Structure to hold shared data
struct shared_data {
  uint16_t recv_msgcnt;
  uint64_t recv_tmstmp;
  struct timespec local_tmstmp;
  pthread_mutex_t lock;
};

// Global shared data structure
struct shared_data data = {0, 0, {0, 0}, PTHREAD_MUTEX_INITIALIZER};

// Thread function to write to CSV
void *write_to_csv(void *arg) {
  (void)arg; // Suppress unused parameter warning
  pthread_mutex_lock(&data.lock);

  FILE *fp = fopen(CSV_FILE, "a");
  if (fp == NULL) {
    perror("Failed to open file");
    pthread_mutex_unlock(&data.lock);
    return NULL;
  }

  struct timespec end;
  clock_gettime(CLOCK_MONOTONIC, &end);
  uint64_t elapsed_time = (end.tv_sec - data.local_tmstmp.tv_sec) * 1000000 +
                          (end.tv_nsec - data.local_tmstmp.tv_nsec) / 1000;
  elapsed_time = data.recv_tmstmp + (elapsed_time / 5); // every 5ms a tick
  fprintf(fp, "%" PRIu64 ",%d\n", elapsed_time, 1);
  fclose(fp);

  pthread_mutex_unlock(&data.lock);
  return NULL;
}

// GPIO event callback
void gpio_event_callback(struct gpiod_line_event *event, void *arg) {
  (void)arg; // Suppress unused parameter warning
  if (event->event_type == GPIOD_LINE_EVENT_FALLING_EDGE) {
    pthread_t thread;
    pthread_create(&thread, NULL, write_to_csv, NULL);
    pthread_detach(thread);
  }
}

void setup_gpio() {
  struct gpiod_chip *chip;
  struct gpiod_line *line;
  int ret;

  chip = gpiod_chip_open_by_name(GPIO_CHIP);
  if (!chip) {
    perror("Open chip failed");
    exit(EXIT_FAILURE);
  }

  line = gpiod_chip_get_line(chip, GPIO_LINE);
  if (!line) {
    perror("Get line failed");
    gpiod_chip_close(chip);
    exit(EXIT_FAILURE);
  }

  ret = gpiod_line_request_falling_edge_events(line, "gpio-monitor");
  if (ret < 0) {
    perror("Request events failed");
    gpiod_chip_close(chip);
    exit(EXIT_FAILURE);
  }

  struct gpiod_line_event event;
  while (1) {
    ret = gpiod_line_event_wait(line, NULL);
    if (ret < 0) {
      perror("Wait event failed");
      break;
    } else if (ret > 0) {
      ret = gpiod_line_event_read(line, &event);
      if (ret < 0) {
        perror("Read event failed");
        break;
      }
      gpio_event_callback(&event, NULL);
    }
  }

  gpiod_chip_close(chip);
}

void *setup_gpio_wrapper(void *arg) {
  (void)arg; // Suppress unused parameter warning
  setup_gpio();
  return NULL;
}

void join_multicast(struct sockaddr_in addr) {

  // Create UDP-Socket
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  // Configure Address
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); // Empfange von allen Interfaces
  addr.sin_port = htons(PORT);

  // Bind Socket
  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind failed");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  // Join Multicast-Group
  mreq.s_addr = inet_addr(MULTICAST_GROUP);
  mreq.s_addr = htonl(INADDR_ANY);
  if (setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)) <
      0) {
    perror("setsockopt");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  printf("Waiting for messages.\nMulticast-Group: %s\nPort: %d\n",
         MULTICAST_GROUP, PORT);
}

void leave_multicast(int signum) {
  (void)signum; // void cast, W-unused-Parameter
  if (setsockopt(sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) <
      0) {
    perror("setsockopt - IP_DROP_MEMBERSHIP");
  }
  close(sockfd);
  printf("Multicast-Gruppe verlassen und Socket geschlossen.\n");
  exit(EXIT_SUCCESS);
}

uint16_t calc_crc16(uint8_t *data, size_t length, uint16_t poly,
                    uint16_t init_val) {
  uint16_t crc = init_val;
  while (length--) {
    crc ^= (*data++) << 8;
    for (uint8_t i = 0; i < 8; i++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ poly;
      } else {
        crc <<= 1;
      }
    }
    crc &= 0xFFFF; // Sicherstellen, dass CRC 16-bit bleibt
  }
  return crc;
}

int main(void) {

  // Setting up Signal-Handler for SIGINT (Ctrl+C)
  signal(SIGINT, leave_multicast);

  struct sockaddr_in addr;
  join_multicast(addr);
  socklen_t addr_len = sizeof(addr);

  uint8_t message[BUFFSIZE];
  uint16_t recv_crc = 0;
  uint16_t crc_value = 0;
  struct timespec msg_tmstmp;

  // Initialize
  clock_gettime(CLOCK_MONOTONIC, &msg_tmstmp);

  pthread_mutex_lock(&data.lock);
  data.recv_msgcnt = 0;
  data.recv_tmstmp = 0;
  data.local_tmstmp = msg_tmstmp;
  pthread_mutex_unlock(&data.lock);

  // Create a thread to monitor GPIO
  pthread_t gpio_thread;
  pthread_create(&gpio_thread, NULL, setup_gpio_wrapper, NULL);

  while (1) {
    int recvlen = recvfrom(sockfd, message, BUFFSIZE, 0,
                           (struct sockaddr *)&addr, &addr_len);
    clock_gettime(CLOCK_MONOTONIC, &msg_tmstmp); // Timestamp Message
    if (recvlen < 0) {
      perror("recvfrom failed");
      leave_multicast(SIGINT);
      close(sockfd);
      exit(EXIT_FAILURE);
    }

    // Check 12 Byte Message
    if (recvlen == 12) {
      crc_value = calc_crc16(message, BUFFSIZE - 2, 0x1021, 0x0000);
      recv_crc = (message[10] << 8) | message[11]; // CRC BIG ENDIANESS

      // Check CRC
      if (recv_crc == crc_value) {
        printf("\nReceived a message!\n");

        pthread_mutex_lock(&data.lock);
        data.recv_msgcnt = (message[1] << 8) | message[0];
        data.recv_tmstmp =
            ((uint64_t)message[9] << 56) | ((uint64_t)message[8] << 48) |
            ((uint64_t)message[7] << 40) | ((uint64_t)message[6] << 32) |
            ((uint64_t)message[5] << 24) | ((uint64_t)message[4] << 16) |
            ((uint64_t)message[3] << 8) | message[2];
        data.local_tmstmp = msg_tmstmp;
        pthread_mutex_unlock(&data.lock);
      }
    }
  }

  // Clean up
  pthread_join(gpio_thread, NULL);
  leave_multicast(SIGINT);
  close(sockfd);

  return 0;
}
