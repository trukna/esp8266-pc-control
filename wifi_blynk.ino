#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define VERSION "Version: 1.1"

// character dimensions (for aligning text)
#define CH_WIDTH 6
#define CH_HEIGHT 8
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET  LED_BUILTIN // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial
#define PC_POWER    5
#define STATUS_LED  2

WiFiUDP udp;
IPAddress pcIP(192, 168, 1, 108);
unsigned int port = 25850;  // local port to listen on
char incomingPacket[255];  // buffer for incoming packets
const char *message[] = {
  "{DKP}",
  "{DKMCST}",
  "{DKMCSP}",
  "{DKPCOFF}"
};
const char *response[] = {
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
int mcuConnected = 0;
int pcStarted = 0;
int pcConnected = 0;
int receivedResponse = 0;
int showPingResponse = 0;
int mcState = 0;

BlynkTimer timer;
int cmdTimer;
int pingTimer;
WidgetTerminal terminal(V0);

void updateDisplay(String str) {
  String mcuIpStr = String("IP: " + String(mcuConnected ? WiFi.localIP().toString() : "0.0.0.0"));
  String pcPowerStateStr = String("PC  : " + String(pcStarted ? "ON" : "OFF"));
  String pcConnStateStr = String("N/W : " + String(pcConnected ? "OK (" : "KO (") + pcIP.toString().substring(pcIP.toString().length() - 3) + String(")"));
  String mcStateStr = String("KODI: " + String(mcState ? "ON" : "OFF"));

  display.clearDisplay();

  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(WHITE); // Draw white text
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  display.setCursor(0, 0);
  display.println(VERSION);
  display.setCursor(0, CH_HEIGHT*1.5);
  display.println(mcuIpStr.c_str());
  display.setCursor(0, CH_HEIGHT*3.5);
  display.println(pcPowerStateStr);
  display.setCursor(0, CH_HEIGHT*5);
  display.println(pcConnStateStr);
  display.setCursor(0, CH_HEIGHT*6.5);
  display.println(mcStateStr);

  if (mcuConnected) {
    display.fillTriangle(
      display.width()-CH_WIDTH*2  , 0,
      display.width()             , 0,
      display.width()-CH_WIDTH    , CH_HEIGHT,
      WHITE);
  } else {
      display.drawTriangle(
      display.width()-CH_WIDTH*2  , 0,
      display.width()             , 0,
      display.width()-CH_WIDTH    , CH_HEIGHT,
      WHITE);
  }

  if (str.length()) {
    display.setCursor((display.width()-CH_WIDTH*(str.length()))/2, (display.height() - CH_HEIGHT)/2);     // Start at top-left corner
    display.println((str.c_str()));
  }

  display.display();
}

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
      {
        bool canUpdateDisplay = false;
        if (!pcConnected) {
          terminal.println("[Startup] complete");
          canUpdateDisplay = true;
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
        if (canUpdateDisplay) {
          updateDisplay("");
          canUpdateDisplay = false;
        }
        break;
      }
      case cmdMC_START:
      case cmdMC_STOP:
        mcState = cmd == cmdMC_START;
        terminal.printf("%s media centre", mcState ? "[Started]" : "[Stopped]");
        terminal.println();
        Blynk.virtualWrite(V2, mcState);
        updateDisplay("");
        break;
      case cmdPC_OFF:
        terminal.println("[Shutdown] complete");
        mcState = 0;
        pcStarted = 0;
        Blynk.virtualWrite(V1, pcStarted);
        Blynk.virtualWrite(V2, mcState);
        digitalWrite(STATUS_LED, HIGH);
        updateDisplay("");
        break;
    }
    terminal.flush();
  }
  timer.disable(cmdTimer);
  timer.restartTimer(pingTimer);
  cmd = cmdNONE;
}
   
BLYNK_CONNECTED()
{
  mcuConnected = 1;
  updateDisplay("");
}

BLYNK_APP_CONNECTED()
{
  Blynk.virtualWrite(V1, pcStarted);
  Blynk.virtualWrite(V2, mcState);
  Blynk.virtualWrite(V3, pcConnected);
  Serial.println("Devkit is online");
  Serial.println(WiFi.localIP());
}

void clearTerminal()
{
  for (int i = 0; i <= 24; i++) {
    terminal.println("");     // "clear screen" in app.
  }
  terminal.flush();
}

void clearPinState()
{
  pcConnected = 0;
  pcStarted = 0;
  mcState = 0;
  cmd = cmdNONE;
  receivedResponse = 0;
  showPingResponse = 0;
  Blynk.virtualWrite(V1, 0);
  Blynk.virtualWrite(V2, 0);
  Blynk.virtualWrite(V3, 0);
  updateDisplay("");
}

void showHelp()
{
  terminal.printf("Supported commands:\n");
  terminal.printf(" clrscr, clrpin\n");
  terminal.flush();  
}

BLYNK_WRITE(V0)
{
  String val = param.asStr();
  if (val == String("clrscr")) {
    clearTerminal();
  } else if (val == String("clrpin")) {
    clearPinState();
  } else if (val == String("help")) {
    showHelp();
  } else {
    terminal.printf("Invalid command\n");
    showHelp();
  }
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
    updateDisplay("");
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
    clearPinState();
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
  Serial.println("ping");
  if (!timer.isEnabled(cmdTimer)) {
    cmd = cmdPING;
    udpWrite();
  }
}

void setup()
{
  // Debug console
  Serial.begin(115200);
  Serial.println("Initializing display");

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  // Clear the buffer
  display.clearDisplay();

  updateDisplay("");
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

  clearTerminal();
}

void loop()
{
  //Blynk.run();
  timer.run();
  int packetSize = udp.parsePacket();
  if (packetSize) {
    udpRead();
  }
}

