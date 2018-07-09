#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <WiFiUdp.h>

/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial
#define PC_POWER    5
#define STATUS_LED  2

WiFiUDP udp;
IPAddress pcIP(192, 168, 1, 104);
unsigned int port = 25850;  // local port to listen on
char incomingPacket[255];  // buffer for incoming packets
char *message[] = {
  "{DKP}",
  "{DKMCST}",
  "{DKMCSP}",
  "{DKPCOFF}"
};
char *response[] = {
  "[PCP]",
  "[PCMCST]",
  "[PCMCSP]",
  "[PCOFF]",
  "[PCON]"
};

enum commandId {
  cmdNONE     = -1,
  cmdPING     = 0,
  cmdMC_START = 1,
  cmdMC_STOP  = 2,
  cmdPC_OFF   = 3,
  cmdPC_ON    = 4
};

// You should get Auth Token in the Blynk App.
// Go to the Project Settings (nut icon).
char auth[] = "token";

// Set password to "" for open networks.
char ssid[] = "ssid";
char pass[] = "password";

commandId cmd = cmdNONE;
int pcStarted = 0;
int pcConnected = 0;
int receivedResponse = 0;
int showPingResponse = 0;
int mcState = 0;

BlynkTimer timer;
int cmdTimer;
int pingTimer;
WidgetTerminal terminal(V0);

void udpWrite()
{
  if (cmd == cmdNONE) {
    return;
  }

  receivedResponse = 0;
  timer.restartTimer(cmdTimer);
  timer.enable(cmdTimer);

  Serial.printf("Sending message %s, size %d\n", message[cmd], strlen(message[cmd]));
  Serial.println(pcIP);
  udp.beginPacket(pcIP, port);
  udp.write(message[cmd], strlen(message[cmd]));
  udp.endPacket();
}

void udpRead()
{
  // receive incoming UDP packets
  Serial.printf("Received from %s, port %d\n", udp.remoteIP().toString().c_str(), udp.remotePort());
  int len = udp.read(incomingPacket, 255);
  if (len > 0) {
    incomingPacket[len] = 0;
  } else {
    return;
  }
  Serial.printf("UDP packet contents: %s\n", incomingPacket);
  if ((cmd != cmdNONE) && (strcmp(incomingPacket, response[cmd]) == 0)) {
    Serial.printf("Received response for %d\n", cmd);
    receivedResponse = 1;
    switch (cmd) {
      case cmdPING:
        if (!pcConnected) {
          terminal.println("[Startup] complete");
        }
        pcConnected = 1;
        pcStarted = 1;
        if (showPingResponse) {
          showPingResponse = 0;
          terminal.println("[Received] ping response");
        }
        Blynk.virtualWrite(V1, pcStarted);
        Blynk.virtualWrite(V3, pcConnected);
        digitalWrite(STATUS_LED, LOW);
        break;
      case cmdMC_START:
      case cmdMC_STOP:
        mcState = cmd == cmdMC_START;
        terminal.printf("%s media centre", mcState ? "[Started]" : "[Stopped]");
        terminal.println();
        Blynk.virtualWrite(V2, mcState);
        break;
      case cmdPC_OFF:
        terminal.println("[Shutdown] complete");
        mcState = 0;
        pcStarted = 0;
        Blynk.virtualWrite(V1, pcStarted);
        Blynk.virtualWrite(V2, mcState);
        digitalWrite(STATUS_LED, HIGH);
        break;
    }
    terminal.flush();
  }
  timer.disable(cmdTimer);
  timer.restartTimer(pingTimer);
  cmd = cmdNONE;
}
   
/*BLYNK_CONNECTED()
{
}*/

BLYNK_APP_CONNECTED()
{
  Blynk.virtualWrite(V1, pcStarted);
  Blynk.virtualWrite(V2, mcState);
  Blynk.virtualWrite(V3, pcConnected);
  Serial.println("Devkit is online");
  Serial.println(WiFi.localIP());
}

BLYNK_WRITE(V1)
{
  int val = param.asInt();
  terminal.printf("%s computer", val ? "[Start]" : "[Shutdown]");
  terminal.println();
  terminal.flush();
  pcStarted = val;
  if (val) {
    digitalWrite(PC_POWER, HIGH);
    delay(1000);
    digitalWrite(PC_POWER, LOW);
    timer.enable(pingTimer);
  } else {
    cmd = cmdPC_OFF;
    udpWrite();
  }
}

BLYNK_WRITE(V2)
{
  int val = param.asInt();
  if (!pcConnected) {
    terminal.println("Computer not yet connected");
    terminal.flush();
    Blynk.virtualWrite(V2, 0);
    return;
  }

  delay(250); 
  Blynk.virtualWrite(V2, !val);
  terminal.printf("%s media centre", val ? "[Start]" : "[Stop]");
  terminal.println();
  terminal.flush();

  cmd = val ? cmdMC_START : cmdMC_STOP;
  udpWrite();
}

BLYNK_WRITE(V3)
{
  int val = param.asInt();
  if (val) {
    cmd = cmdPING;
    showPingResponse = 1;
    digitalWrite(STATUS_LED, HIGH);
    udpWrite();
  } else {
    pcConnected = 0;
    Blynk.virtualWrite(V3, pcConnected);
  }
}

BLYNK_WRITE(V4)
{
  int val = param.asInt();
  if (val) {
    terminal.println("[Restart] devkit...");
    terminal.flush();
    delay(250);
    Blynk.virtualWrite(V4, !val);
    pcConnected = 0;
    cmd = cmdNONE;
    receivedResponse = 0;
    showPingResponse = 0;
    Blynk.virtualWrite(V1, 0);
    Blynk.virtualWrite(V2, 0);
    Blynk.virtualWrite(V3, 0);
    ESP.restart();
  }
}

void cmdTimerExpired()
{
  if (receivedResponse == 1) {
    Serial.println("Timer expired even after receiving response");
  }
  timer.disable(cmdTimer);
  switch (cmd) {
    case cmdPING:
    {
      if (showPingResponse) {
        terminal.println("[Failed] to ping");
      }
      showPingResponse = 0;
      pcConnected = 0;
      Blynk.virtualWrite(V3, pcConnected);
      digitalWrite(STATUS_LED, LOW);
      delay(250);
      digitalWrite(STATUS_LED, HIGH);
    }
      break;
    case cmdMC_START:
    case cmdMC_STOP:
      mcState = !(cmd == cmdMC_START);
      Blynk.virtualWrite(V2, mcState);
      terminal.printf("[Failed] to %s media centre", mcState ? "stop" : "start");
      terminal.println();
      break;
    case cmdPC_OFF:
      pcStarted = 1;
      Blynk.virtualWrite(V1, pcStarted);
      terminal.println("[Failed] to shutdown computer");
      break;
  }

  timer.restartTimer(pingTimer);
  terminal.flush();
  return;
}

void pingTimerExpired()
{
  if (!timer.isEnabled(cmdTimer)) {
    cmd = cmdPING;
    udpWrite();
  }
}

void setup()
{
  // Debug console
  Serial.begin(115200);
  Blynk.begin(auth, ssid, pass);

  pinMode(PC_POWER, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(PC_POWER, LOW);
  digitalWrite(STATUS_LED, HIGH);

  cmdTimer = timer.setInterval(1000, cmdTimerExpired);
  pingTimer = timer.setInterval(10000, pingTimerExpired);
  timer.disable(cmdTimer);
  timer.enable(pingTimer);
  udp.begin(port);

  for (int i = 0; i <= 24; i++) {
    terminal.println("");     // "clear screen" in app.
  }
  terminal.flush();
}

void loop()
{
  Blynk.run();
  timer.run();
  int packetSize = udp.parsePacket();
  if (packetSize) {
    udpRead();
  }
}

