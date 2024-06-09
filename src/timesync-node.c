#include <arpa/inet.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h> // clock_gettime(CLOCK_MONOTONIC, &start_time);
#include <unistd.h>

// https://docs.google.com/document/d/1HvjNvKx5gJ1GtRJtmMu9Jwueku3i7r2VC9iElPTr-Uw/edit#heading=h.fbg9cypbzerm
// Tick alle 5ms, kein microtick

/*
To Do:
- Synchronisieren der Zeit
- Trigger auf Pin Toggle & Speichern der Zeitstempel
- Error Handling für maximale Verfügbarkeit
*/

#define MULTICAST_GROUP "224.0.0.1"
#define PORT 12345
#define BUFFSIZE 12

int sockfd;
struct in_addr mreq;

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
  uint16_t recv_msgcnt = 0;
  uint64_t recv_tmstmp = 0;
  uint16_t recv_crc = 0;
  uint16_t crc_value = 0;

  // Initialize Timestamps
  struct timespec msg_tmstmp, local_tmstmp;
  clock_gettime(CLOCK_MONOTONIC, &msg_tmstmp);
  local_tmstmp = msg_tmstmp;

  while (1) {
    int recvlen = recvfrom(sockfd, message, BUFFSIZE, 0,
                           (struct sockaddr *)&addr, &addr_len);
    if (recvlen < 0) {
      perror("recvfrom failed");
      leave_multicast(SIGINT);
      close(sockfd);
      exit(EXIT_FAILURE);
    }

    // Check 12 Byte Message
    if (recvlen == 12) {
      clock_gettime(CLOCK_MONOTONIC, &msg_tmstmp); // Timestamp Message

      crc_value = calc_crc16(message, BUFFSIZE - 2, 0x1021, 0x0000);
      recv_crc = (message[10] << 8) | message[11]; // CRC BIG ENDIANESS

      // Check CRC
      if (recv_crc == crc_value) {
        recv_msgcnt = (message[1] << 8) | message[0];
        recv_tmstmp =
            ((uint64_t)message[9] << 56) | ((uint64_t)message[8] << 48) |
            ((uint64_t)message[7] << 40) | ((uint64_t)message[6] << 32) |
            ((uint64_t)message[5] << 24) | ((uint64_t)message[4] << 16) |
            ((uint64_t)message[3] << 8) | message[2];
        local_tmstmp = msg_tmstmp;
      }
    }
  }

  leave_multicast(SIGINT);
  close(sockfd);

  return 0;
}
