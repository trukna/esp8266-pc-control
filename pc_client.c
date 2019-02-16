#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
 
#define PORT    25850
#define MAXLINE 255
#define SLEEP   2

#define START    "/usr/bin/kodi -d SLEEP &"
#define STOP     "killall kodi.bin &"
#define SHUTDOWN "systemctl poweroff"

static int sockfd;
int mcStarted = 0;

void SendPacket(int id, struct sockaddr_in *addr, socklen_t addr_size)
{
    char buffer[MAXLINE];
    struct sockaddr_in cliaddr;

    switch(id) {
    case 0:
        printf("Sent power on message\n");
        strcpy(buffer, "[PCON]");
    break;
    case 1:
        printf("Sent ping message\n");
        strcpy(buffer, "[PCP]");
    break;
    case 2:
        printf("Sent started message\n");
        strcpy(buffer, "[PCMCST]");
    break;
    case 3:
        printf("Sent stopped message\n");
        strcpy(buffer, "[PCMCSP]");
    break;
    case 4:
        printf("Sent power off message\n");
        strcpy(buffer, "[PCOFF]");
    break;
    }

    sendto(sockfd, (const char *)buffer, strlen(buffer),
        MSG_CONFIRM, (const struct sockaddr *)addr, 
            addr_size);

    memset(&cliaddr, 0, sizeof(cliaddr));
    cliaddr.sin_family = AF_INET;
    cliaddr.sin_port = htons(PORT);
    cliaddr.sin_addr.s_addr = inet_addr("192.168.1.108");

    printf("Sending again to ip %s, port %d\n", inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port);
    sendto(sockfd, (const char *)buffer, strlen(buffer),
        MSG_CONFIRM, (const struct sockaddr *) &cliaddr, 
            sizeof(cliaddr));
            
    sleep(SLEEP);
    if (id == 2) {
        system(START);
    } else if (id == 3) {
        system(STOP);
    } else if (id == 4) {
        system(SHUTDOWN);
    }
}

void ProcessPacket(char *buf, struct sockaddr_in *addr, socklen_t addr_size)
{
    printf("Sender ip %s, port %d\n", inet_ntoa(addr->sin_addr), addr->sin_port);
    if (strncmp(buf, "{DKP}", 6) == 0) {
        printf("Received ping message\n");
        SendPacket(1, addr, addr_size);
    } else if ((strncmp(buf, "{DKMCST}", 7) == 0) && !mcStarted) {
        printf("Received start message\n");
        mcStarted = 1;
        SendPacket(2, addr, addr_size);
    } else if ((strncmp(buf, "{DKMCSP}", 6) == 0) && mcStarted) {
        printf("Received stop message\n");
        mcStarted = 0;
        SendPacket(3, addr, addr_size);
    } else if (strncmp(buf, "{DKPCOFF}", 10) == 0) {
        printf("Received shutdown message\n");
        SendPacket(4, addr, addr_size);
    }
}

int main()
{
    int canRun = 1;
    char buffer[MAXLINE];
    struct sockaddr_in servaddr, cliaddr;
    socklen_t addr_size;
 
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    while (canRun) {
        memset(&buffer, '\0', sizeof(buffer));      

        int n;            
        n = recvfrom(sockfd, (char *)buffer, MAXLINE, 
                    MSG_WAITALL, (struct sockaddr *) &cliaddr,
                    &addr_size);
        buffer[n] = '\0';
        ProcessPacket(buffer, &cliaddr, addr_size);
    }

    close(sockfd);
    return 0;
}
