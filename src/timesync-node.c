#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

// https://docs.google.com/document/d/1HvjNvKx5gJ1GtRJtmMu9Jwueku3i7r2VC9iElPTr-Uw/edit#heading=h.fbg9cypbzerm
// Tick alle 5ms, kein microtick

/*
To Do:
- CRC checken
- Synchronisieren der Zeit
- Trigger auf Pin Toggle
- Speichern der Zeitstempel in Array
- Endlosschleife verlassen
- Error Handling für maximale Verfügbarkeit
- speichern asynchron
*/

#define MULTICAST_GROUP "224.0.0.1"
#define PORT 12345
#define BUFFSIZE 11 * 8
#define PIN 0


int sockfd;
struct in_addr mreq;


void cleanup(int signum) {
  (void)signum;   // signum parameter void casten, wegen W unused Parameter
  // Multicast-Gruppe verlassen
  if (setsockopt(sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) <
      0) {
    perror("setsockopt - IP_DROP_MEMBERSHIP");
  }
  close(sockfd);
  printf("Multicast-Gruppe verlassen und Socket geschlossen.\n");
  exit(EXIT_SUCCESS);
}


void increment_value_every_5ms(int *value, int iterations) {
  struct timespec start_time, current_time;
  long elapsed_time_ms;

  // Startzeit abrufen
  clock_gettime(CLOCK_MONOTONIC, &start_time);

  int count = 0;
  while (count < iterations) {
    // Aktuelle Zeit abrufen
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    // Berechnung der vergangenen Zeit in Millisekunden
    elapsed_time_ms = (current_time.tv_sec - start_time.tv_sec) * 1000 +
                      (current_time.tv_nsec - start_time.tv_nsec) / 1000000;

    // Überprüfen, ob 5 ms vergangen sind
    if (elapsed_time_ms >= 5) {
      (*value)++;
      printf("Current value: %d\n", *value);

      // Startzeit für die nächste Messung aktualisieren
      start_time = current_time;
      count++;
    }
  }
}


uint16_t crc16(const char *data, size_t length, uint16_t poly,
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

  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  char buffer[BUFFSIZE];

  // Signal-Handler für SIGINT (Ctrl+C) einrichten
  signal(SIGINT, cleanup);

  // Erstelle ein UDP-Socket
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  // Konfiguriere die Adresse
  memset(&addr, 0, addr_len);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); // Empfange von allen Interfaces
  addr.sin_port = htons(PORT);

  // Binde das Socket an die Adresse
  if (bind(sockfd, (struct sockaddr *)&addr, addr_len) < 0) {
    perror("bind failed");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  // Beitreten zur Multicast-Gruppe
  mreq.s_addr = inet_addr(MULTICAST_GROUP);
  mreq.s_addr = htonl(INADDR_ANY);
  if (setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)) <
      0) {
    perror("setsockopt");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  printf("Warte auf Nachrichten von der Multicast-Gruppe %s auf Port %d...\n",
         MULTICAST_GROUP, PORT);

  // Empfange Multicast-Nachrichten
  while (1) {
    int recvlen = recvfrom(sockfd, buffer, BUFFSIZE, 0,
                           (struct sockaddr *)&addr, &addr_len);
    if (recvlen < 0) {
      perror("recvfrom failed");
      cleanup(SIGINT); // Bereinigen und beenden bei Fehler
      close(sockfd);
      exit(EXIT_FAILURE);
    }
    uint16_t crc_value = crc16(buffer, recvlen, 0x1021, 0x0000);
    buffer[recvlen] = '\0'; // Null-terminiere den String
    printf("Empfangen von %s:%d: '%s'\n", inet_ntoa(addr.sin_addr),
           ntohs(addr.sin_port), buffer);
    printf("crc: %d", crc_value);
  }

  cleanup(SIGINT); // Bereinigen und beenden bei Fehler
  close(sockfd);

  int value = 0;
  int iterations = 100; // Anzahl der Inkrementierungen

  increment_value_every_5ms(&value, iterations);

  int length = 5;
  int timestamps = 1;

  // Open file for writing
  FILE *csvfile = fopen("./timestamps_3.csv", "w");
  if (csvfile == NULL) {
    printf("writing to file failed\n");
    return 1;
  }

  // Write data to the file
  fprintf(csvfile, "Measured time [us]\n");
  for (int i = 0; i < length; ++i) {
    fprintf(csvfile, "%d\n", timestamps);
  }

  return 0;
}
