#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
To Do:
- CRC Berechnung
- Synchronisieren der Zeit
- Trigger auf Pin Toggle
- Speichern der Zeitstempel in Array
- Endlosschleife verlassen
*/

#define PORT 12345
#define BUFFSIZE 1024

int main(void) {
  int sockfd;
  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  char buffer[BUFFSIZE];

  // Erstelle ein UDP-Socket
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  // Setze die Socket-Option für Broadcast
  int broadcastEnable = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable,
                 sizeof(broadcastEnable)) < 0) {
    perror("setsockopt (SO_BROADCAST) failed");
    close(sockfd);
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

  printf("Warte auf Broadcast-Nachrichten auf Port %d...\n", PORT);

  // Empfange Broadcast-Nachrichten
  while (1) {
    int recvlen = recvfrom(sockfd, buffer, BUFFSIZE, 0,
                           (struct sockaddr *)&addr, &addr_len);
    if (recvlen < 0) {
      perror("recvfrom failed");
      close(sockfd);
      exit(EXIT_FAILURE);
    }
    buffer[recvlen] = '\0'; // Null-terminiere den String
    printf("Empfangen von %s:%d: '%s'\n", inet_ntoa(addr.sin_addr),
           ntohs(addr.sin_port), buffer);
  }

  close(sockfd);

  int length = 5;
  int timestamps = 1;

  // Open file for writing
  FILE *csvfile = fopen("./timestamps.csv", "w");
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
