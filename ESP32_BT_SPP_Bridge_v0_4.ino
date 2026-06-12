/*
  ESP32 Bluetooth SPP Bridge v0.4
  Serial2 is always the retro-computer UART side.
  Console side: AUTO / BT / USB.
*/

#include <Arduino.h>
#include <BluetoothSerial.h>
#include <Preferences.h>
#include <esp_bt_device.h>
#include <stdarg.h>

#define RETRO_UART_RX 16
#define RETRO_UART_TX 17
#define LED_BT   2
#define LED_RX   25
#define LED_TX   26
#define LED_CMD  27
#define ACTIVITY_LED_MS 30

BluetoothSerial SerialBT;
Preferences prefs;

enum ConsoleMode { CONSOLE_AUTO, CONSOLE_BT, CONSOLE_USB };

struct Config {
  String name;
  String pin;
  uint32_t baud;
  ConsoleMode console;
  bool quiet;
  bool echo;
  bool notify;
  uint32_t s12GuardMs;
};

Config running;
Config startup;

bool commandMode = false;
bool btConnected = false;
bool carrierPresent = false;

String cmdLine;
String plusBuf;

uint32_t lastConsoleDataMs = 0;
uint32_t plusDetectedMs = 0;
bool waitingPostGuard = false;
bool suppressPlusForward = false;

uint32_t rxLedUntil = 0;
uint32_t txLedUntil = 0;

const char *FW_VERSION = "ESP32 Bluetooth SPP Bridge 0.4";

Config factoryConfig() {
  Config c;
  c.name = "ESP32_SPP_TEST";
  c.pin = "1234";
  c.baud = 115200;
  c.console = CONSOLE_AUTO;
  c.quiet = true;
  c.echo = false;
  c.notify = true;
  c.s12GuardMs = 1000;
  return c;
}

const char *consoleName(ConsoleMode s) {
  switch (s) {
    case CONSOLE_AUTO: return "AUTO";
    case CONSOLE_USB:  return "USB";
    case CONSOLE_BT:
    default:           return "BT";
  }
}

ConsoleMode parseConsole(const String &s, ConsoleMode def) {
  if (s == "AUTO") return CONSOLE_AUTO;
  if (s == "USB")  return CONSOLE_USB;
  if (s == "BT")   return CONSOLE_BT;
  return def;
}

ConsoleMode effectiveConsole() {
  if (running.console == CONSOLE_AUTO) return btConnected ? CONSOLE_BT : CONSOLE_USB;
  return running.console;
}

const char *effectiveConsoleName() { return consoleName(effectiveConsole()); }
const char *modeName() { return commandMode ? "COMMAND" : "ONLINE"; }
Stream& consolePort() { return (effectiveConsole() == CONSOLE_USB) ? (Stream&)Serial : (Stream&)SerialBT; }
bool isUsbConsoleEffective() { return effectiveConsole() == CONSOLE_USB; }
bool isBtConsoleEffective() { return effectiveConsole() == CONSOLE_BT; }

void logPrintf(const char *fmt, ...) {
  if (running.quiet) return;
  char buf[180];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
}

void logMsg(const String &msg) { if (!running.quiet) Serial.println(msg); }
void notifyUsb(const char *msg) { if (running.notify) Serial.println(msg); }

void notifyConsoleState() {
  if (!running.notify) return;
  if (running.console == CONSOLE_AUTO) {
    Serial.println("[CONSOLE AUTO]");
    Serial.print("[CONSOLE "); Serial.print(effectiveConsoleName()); Serial.println("]");
  } else {
    Serial.print("[CONSOLE "); Serial.print(consoleName(running.console)); Serial.println("]");
  }
}

void loadStartup() {
  Config def = factoryConfig();
  prefs.begin("btbridge", true);
  startup.name = prefs.getString("name", def.name);
  startup.pin  = prefs.getString("pin", def.pin);
  startup.baud = prefs.getUInt("baud", def.baud);
  String console = prefs.getString("console", consoleName(def.console));
  startup.console = parseConsole(console, def.console);
  startup.quiet = prefs.getBool("quiet", def.quiet);
  startup.echo = prefs.getBool("echo", def.echo);
  startup.notify = prefs.getBool("notify", def.notify);
  startup.s12GuardMs = prefs.getUInt("s12", def.s12GuardMs);
  prefs.end();
}

void saveStartup() {
  prefs.begin("btbridge", false);
  prefs.putString("name", running.name);
  prefs.putString("pin", running.pin);
  prefs.putUInt("baud", running.baud);
  prefs.putString("console", consoleName(running.console));
  prefs.putBool("quiet", running.quiet);
  prefs.putBool("echo", running.echo);
  prefs.putBool("notify", running.notify);
  prefs.putUInt("s12", running.s12GuardMs);
  prefs.end();
  startup = running;
}

void setupLeds() {
  pinMode(LED_BT, OUTPUT); pinMode(LED_RX, OUTPUT); pinMode(LED_TX, OUTPUT); pinMode(LED_CMD, OUTPUT);
  digitalWrite(LED_BT, LOW); digitalWrite(LED_RX, LOW); digitalWrite(LED_TX, LOW); digitalWrite(LED_CMD, LOW);
}

void updateLeds() {
  static uint32_t lastBlink = 0;
  static bool blink = false;
  uint32_t now = millis();
  if (btConnected) digitalWrite(LED_BT, HIGH);
  else {
    if (now - lastBlink >= 500) { lastBlink = now; blink = !blink; }
    digitalWrite(LED_BT, blink ? HIGH : LOW);
  }
  digitalWrite(LED_CMD, commandMode ? HIGH : LOW);
  digitalWrite(LED_RX, now < rxLedUntil ? HIGH : LOW);
  digitalWrite(LED_TX, now < txLedUntil ? HIGH : LOW);
}

void btCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  switch (event) {
    case ESP_SPP_SRV_OPEN_EVT:
      btConnected = true; carrierPresent = true; logMsg("SPP CONNECTED");
      notifyUsb("[BT CONNECTED]");
      if (running.console == CONSOLE_AUTO) notifyUsb("[CONSOLE BT]");
      break;
    case ESP_SPP_CLOSE_EVT:
      btConnected = false; carrierPresent = false; commandMode = false; cmdLine = ""; plusBuf = ""; waitingPostGuard = false; suppressPlusForward = false;
      logMsg("SPP DISCONNECTED");
      notifyUsb("[BT DISCONNECTED]");
      if (running.console == CONSOLE_AUTO) notifyUsb("[CONSOLE USB]");
      break;
    default: break;
  }
}

void printOK(Stream &s) { s.println("OK"); }
void printERR(Stream &s) { s.println("ERROR"); }

void printBtMac(Stream &s) {
  const uint8_t *mac = esp_bt_dev_get_address();
  if (!mac) { s.println("UNAVAILABLE"); return; }
  s.printf("%02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void showConfig(Stream &s, const char *title, const Config &c) {
  s.println(title);
  s.print("NAME=");      s.println(c.name);
  s.print("PIN=");       s.println(c.pin);
  s.print("BAUD=");      s.println(c.baud);
  s.print("CONSOLE=");   s.println(consoleName(c.console));
  s.print("EFFECTIVE="); s.println((&c == &running) ? effectiveConsoleName() : "-");
  s.print("QUIET=");     s.println(c.quiet ? "1" : "0");
  s.print("ECHO=");      s.println(c.echo ? "1" : "0");
  s.print("NOTIFY=");    s.println(c.notify ? "1" : "0");
  s.print("S12=");       s.println(c.s12GuardMs);
}

bool validBaud(uint32_t b) {
  switch (b) {
    case 300: case 1200: case 2400: case 4800: case 9600: case 19200:
    case 38400: case 57600: case 115200: case 230400: case 460800: case 921600:
      return true;
    default: return false;
  }
}

bool validGuardTime(uint32_t ms) { return ms <= 5000; }

void resetCommandStateAfterOnline() {
  cmdLine = ""; plusBuf = ""; waitingPostGuard = false; suppressPlusForward = false; lastConsoleDataMs = millis();
}

void handleCommand(String cmd, Stream &io) {
  cmd.trim();
  if (cmd == "AT") { printOK(io); }
  else if (cmd == "ATI") {
    io.println(FW_VERSION); io.println("TYPE=Bluetooth SPP Bridge"); io.println("UART=Serial2 retro side"); io.println("CONTROL=Hayes-like");
    io.print("BTMAC="); printBtMac(io); printOK(io);
  }
  else if (cmd == "ATD") {
    showConfig(io, "Running:", running);
    io.print("MODE="); io.println(modeName());
    io.print("BT="); io.println(btConnected ? "CONNECTED" : "DISCONNECTED");
    io.print("CARRIER="); io.println(carrierPresent ? "ON" : "OFF");
    printOK(io);
  }
  else if (cmd == "AT&V") {
    showConfig(io, "Running:", running); showConfig(io, "Startup:", startup);
    io.print("MODE="); io.println(modeName());
    io.print("BT="); io.println(btConnected ? "CONNECTED" : "DISCONNECTED");
    io.print("CARRIER="); io.println(carrierPresent ? "ON" : "OFF");
    printOK(io);
  }
  else if (cmd == "AT&W") { saveStartup(); printOK(io); }
  else if (cmd == "AT&Y") { running = startup; Serial2.updateBaudRate(running.baud); printOK(io); notifyConsoleState(); }
  else if (cmd == "AT&F") { running = factoryConfig(); Serial2.updateBaudRate(running.baud); printOK(io); notifyConsoleState(); }
  else if (cmd == "ATZ") { printOK(io); delay(300); ESP.restart(); }
  else if (cmd == "ATO") {
    commandMode = false;
    carrierPresent = (effectiveConsole() == CONSOLE_BT) ? btConnected : true;
    resetCommandStateAfterOnline();
    io.println(carrierPresent ? "CONNECT" : "NO CARRIER");
  }
  else if (cmd == "ATO?") { io.println(modeName()); printOK(io); }
  else if (cmd == "ATH") { carrierPresent = false; commandMode = false; resetCommandStateAfterOnline(); io.println("NO CARRIER"); }
  else if (cmd == "ATN?") { io.println(running.name); printOK(io); }
  else if (cmd.startsWith("ATN=")) {
    String v = cmd.substring(4); v.trim();
    if (v.length() > 0 && v.length() <= 32) { running.name = v; printOK(io); io.println("NOTE: AT&W + ATZ to apply."); }
    else printERR(io);
  }
  else if (cmd == "ATP?") { io.println(running.pin); printOK(io); }
  else if (cmd.startsWith("ATP=")) {
    String v = cmd.substring(4); v.trim();
    if (v.length() >= 4 && v.length() <= 16) { running.pin = v; printOK(io); io.println("NOTE: AT&W + ATZ to apply."); }
    else printERR(io);
  }
  else if (cmd == "ATB?") { io.println(running.baud); printOK(io); }
  else if (cmd.startsWith("ATB=")) {
    uint32_t b = cmd.substring(4).toInt();
    if (validBaud(b)) { running.baud = b; Serial2.updateBaudRate(running.baud); printOK(io); }
    else printERR(io);
  }
  else if (cmd == "ATCONSOLE?") { io.print("CONFIG="); io.println(consoleName(running.console)); io.print("EFFECTIVE="); io.println(effectiveConsoleName()); printOK(io); }
  else if (cmd == "ATCONSOLE=AUTO") { running.console = CONSOLE_AUTO; printOK(io); notifyConsoleState(); }
  else if (cmd == "ATCONSOLE=BT") { running.console = CONSOLE_BT; printOK(io); notifyConsoleState(); }
  else if (cmd == "ATCONSOLE=USB") { running.console = CONSOLE_USB; printOK(io); notifyConsoleState(); }
  else if (cmd == "ATSRC?") { io.print("CONFIG="); io.println(consoleName(running.console)); io.print("EFFECTIVE="); io.println(effectiveConsoleName()); printOK(io); }
  else if (cmd == "ATSRC=AUTO") { running.console = CONSOLE_AUTO; printOK(io); notifyConsoleState(); }
  else if (cmd == "ATSRC=BT" || cmd == "ATSRC=UART") { running.console = CONSOLE_BT; printOK(io); notifyConsoleState(); }
  else if (cmd == "ATSRC=USB") { running.console = CONSOLE_USB; printOK(io); notifyConsoleState(); }
  else if (cmd == "ATQ?") { io.println(running.quiet ? "1" : "0"); printOK(io); }
  else if (cmd == "ATQ=0") { running.quiet = false; printOK(io); }
  else if (cmd == "ATQ=1") { running.quiet = true; printOK(io); }
  else if (cmd == "ATE?") { io.println(running.echo ? "1" : "0"); printOK(io); }
  else if (cmd == "ATE0" || cmd == "ATE=0") { running.echo = false; printOK(io); }
  else if (cmd == "ATE1" || cmd == "ATE=1") { running.echo = true; printOK(io); }
  else if (cmd == "ATNOTIFY?") { io.println(running.notify ? "1" : "0"); printOK(io); }
  else if (cmd == "ATNOTIFY=0") { running.notify = false; printOK(io); }
  else if (cmd == "ATNOTIFY=1") { running.notify = true; printOK(io); notifyConsoleState(); }
  else if (cmd == "ATL?") {
    if (effectiveConsole() == CONSOLE_BT && !btConnected) io.println("DISCONNECTED");
    else if (carrierPresent) io.println("CONNECTED,CARRIER");
    else io.println("CONNECTED,NO_CARRIER");
    printOK(io);
  }
  else if (cmd == "ATC?") { io.println(btConnected ? "1" : "0"); printOK(io); }
  else if (cmd == "ATMAC?") { printBtMac(io); printOK(io); }
  else if (cmd == "ATS12?") { io.println(running.s12GuardMs); printOK(io); }
  else if (cmd.startsWith("ATS12=")) {
    uint32_t ms = cmd.substring(6).toInt();
    if (validGuardTime(ms)) { running.s12GuardMs = ms; printOK(io); }
    else printERR(io);
  }
  else if (cmd == "ATHELP" || cmd == "ATH?" || cmd == "AT?") {
    io.println("AT            Test");
    io.println("ATI           Version");
    io.println("ATD           Show config");
    io.println("AT&V          Show all");
    io.println("AT&W          Save");
    io.println("AT&Y          Load");
    io.println("AT&F          Defaults");
    io.println("ATZ           Reboot");
    io.println("ATO           Online");
    io.println("ATO?          Mode");
    io.println("ATH           Hangup");
    io.println("ATN?          Name");
    io.println("ATN=<name>    Set name");
    io.println("ATP?          PIN");
    io.println("ATP=<pin>     Set PIN");
    io.println("ATB?          Baud");
    io.println("ATB=<baud>    Set baud");
    io.println("ATCONSOLE?    Console");
    io.println("ATCONSOLE=<AUTO|BT|USB>");
    io.println("ATE?          Echo");
    io.println("ATE=<0|1>");
    io.println("ATQ?          Quiet");
    io.println("ATQ=<0|1>");
    io.println("ATNOTIFY?     Notify");
    io.println("ATNOTIFY=<0|1>");
    io.println("ATL?          Link");
    io.println("ATC?          Conn");
    io.println("ATMAC?        BT MAC");
    io.println("ATS12?        Escape");
    io.println("ATS12=<ms>    0=imm");
    printOK(io);
  }
  else printERR(io);
}

void enterCommandMode(Stream &io) {
  commandMode = true;
  carrierPresent = (effectiveConsole() == CONSOLE_BT) ? btConnected : true;
  plusBuf = ""; cmdLine = ""; waitingPostGuard = false; suppressPlusForward = false;
  io.println("OK");
}

void cancelPendingEscapeAndForwardPlus(Stream &toRetro) {
  if (waitingPostGuard) { toRetro.write((const uint8_t *)"+++", 3); txLedUntil = millis() + ACTIVITY_LED_MS; }
  waitingPostGuard = false; suppressPlusForward = false; plusBuf = "";
}

void processOnlineConsoleChar(char c, Stream &console, Stream &toRetro) {
  uint32_t now = millis();
  if (waitingPostGuard) cancelPendingEscapeAndForwardPlus(toRetro);
  plusBuf += c;
  if (plusBuf.length() > 3) plusBuf.remove(0, plusBuf.length() - 3);
  if (plusBuf == "+++") {
    if (running.s12GuardMs == 0) {
      plusBuf = ""; lastConsoleDataMs = now; enterCommandMode(console); return;
    }
    uint32_t preGap = now - lastConsoleDataMs;
    if (preGap >= running.s12GuardMs) {
      waitingPostGuard = true; plusDetectedMs = now; suppressPlusForward = true; plusBuf = ""; lastConsoleDataMs = now; return;
    }
  }
  if (!suppressPlusForward) { toRetro.write((uint8_t)c); txLedUntil = millis() + ACTIVITY_LED_MS; }
  lastConsoleDataMs = now;
}

void serviceEscapeGuard() {
  if (!waitingPostGuard) return;
  uint32_t now = millis();
  if (now - plusDetectedMs >= running.s12GuardMs) enterCommandMode(consolePort());
}

void processCommandChar(char c, Stream &io) {
  if (c == '\r' || c == '\n') {
    if (cmdLine.length()) {
      if (running.echo) io.println();
      handleCommand(cmdLine, io);
      cmdLine = "";
    }
  } else {
    if (running.echo) io.write((uint8_t)c);
    cmdLine += c;
  }
}

void handleBtInput() {
  while (SerialBT.available()) {
    char c = SerialBT.read();
    if (isBtConsoleEffective()) {
      if (!commandMode) processOnlineConsoleChar(c, SerialBT, Serial2);
      else processCommandChar(c, SerialBT);
    } else continue;
  }
}

void handleUsbInput() {
  while (Serial.available()) {
    char c = Serial.read();
    if (isUsbConsoleEffective()) {
      if (!commandMode) processOnlineConsoleChar(c, Serial, Serial2);
      else processCommandChar(c, Serial);
    } else continue;
  }
}

void handleRetroInput() {
  while (Serial2.available()) {
    char c = Serial2.read();
    if (commandMode) continue;
    if (isBtConsoleEffective()) SerialBT.write((uint8_t)c);
    else Serial.write((uint8_t)c);
    rxLedUntil = millis() + ACTIVITY_LED_MS;
  }
}

void setup() {
  setupLeds();
  Serial.begin(115200);
  delay(300);
  loadStartup();
  running = startup;
  commandMode = false; carrierPresent = false;
  Serial2.begin(running.baud, SERIAL_8N1, RETRO_UART_RX, RETRO_UART_TX);
  SerialBT.register_callback(btCallback);
  bool btOk = SerialBT.begin(running.name.c_str());
  bool pinOk = SerialBT.setPin(running.pin.c_str(), running.pin.length());
  lastConsoleDataMs = millis();
  logPrintf("BT start: %s\n", btOk ? "OK" : "FAIL");
  logPrintf("PIN set : %s\n", pinOk ? "OK" : "FAIL");
  if (!running.quiet) { showConfig(Serial, "Running:", running); Serial.println("MODE=ONLINE"); }
  if (running.notify) notifyConsoleState();
}

void loop() {
  handleBtInput();
  handleUsbInput();
  handleRetroInput();
  serviceEscapeGuard();
  updateLeds();
}
