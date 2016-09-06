#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WebSocketClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

#define MODE_AP_WS  100
#define MODE_WSC    101

#define STATUS_PIN   13
#define SIGNAL_PIN   12
#define RESET_PIN     4
#define CONTROL_PIN   0

#define EEPROM_CHECK  2
#define EEPROM_SSID  34
#define EEPROM_PASS  98
#define EEPROM_CID  118
#define EEPROM_HOST 178

#define CHANNEL_TYPE_SWITCH 0
#define CHANNEL_TYPE_SLIDER 1

const char *ssid_name = "iot-ware-manager";

String channel_name = "universal relay";
String channel_type = String(CHANNEL_TYPE_SWITCH);

String path = "/hub/open?";
String g_host = "naumchevski.com";
String g_cid = "";
String full_path;

int app_mode;
bool restarted = false;

String g_ssid;
String g_password;

ESP8266WebServer server(80);
WebSocketClient webSocketClient;
WiFiClient client;

/* setup start */
void setup() {
  setup_begin();
  setup_init();
  setup_end();
}

void setup_begin(void) {
  delay(1000);
  Serial.begin(115200);
  EEPROM.begin(512);
  pinMode(STATUS_PIN, OUTPUT);
  pinMode(SIGNAL_PIN, OUTPUT);
  pinMode(CONTROL_PIN, OUTPUT);
  pinMode(RESET_PIN, INPUT);
  delay(1000);
  check_reset_device();
  Serial.println("");
}

void setup_init(void) {  
  String check = read_eeprom(0, EEPROM_CHECK);
  if (check == "1") {
    g_ssid = read_eeprom(EEPROM_CHECK, EEPROM_SSID);
    g_password = read_eeprom(EEPROM_SSID, EEPROM_PASS);
    g_cid = read_eeprom(EEPROM_PASS, EEPROM_CID);
    g_host = read_eeprom(EEPROM_CID, EEPROM_HOST);
    full_path = path + g_cid;
    
    Serial.print("r:ssid: "); Serial.println(g_ssid);
    Serial.print("r:password: "); Serial.println(g_password);
    Serial.print("r:host: "); Serial.println(g_host);
    Serial.print("r:path: "); Serial.println(full_path);
    
    start_wsc();
  } else {
    start_ac();
  }
}

void setup_end(void) {
  
}
/* setup end */

/* web socket client start */
void start_wsc() {
  Serial.println("start web socket client");
  app_mode = MODE_WSC;
  
  WiFi.begin(g_ssid.c_str(), g_password.c_str());
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(250); digitalWrite(STATUS_PIN, HIGH);
    delay(250); digitalWrite(STATUS_PIN, LOW);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(g_ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  delay(5000);
  // Connect to the websocket server
  if (client.connect(g_host.c_str(), 80)) {
    Serial.println("Connected");
  } else {
    Serial.println("Connection failed.");
    while (1) {
      // Hang on failure
    }
  }

  // Handshake with the server
  webSocketClient.path = (char*)full_path.c_str();
  webSocketClient.host = (char*)g_host.c_str();
  if (webSocketClient.handshake(client)) {
    Serial.println("Handshake successful");
    digitalWrite(STATUS_PIN, HIGH);
  } else {
    Serial.println("Handshake failed.");
    digitalWrite(STATUS_PIN, LOW);
    while (1) {
      // Hang on failure
    }
  }
}

void wsc_handle() {
  String data;

  if (client.connected()) {

    webSocketClient.getData(data);
    if (data.length() > 0) {
      Serial.print("Received data: ");
      Serial.println(data);

      StaticJsonBuffer<200> jsonBuffer;
      JsonObject& root = jsonBuffer.parseObject(data);

      if (!root.success()) {
        Serial.println("parseObject() failed");
      } else {
        const char* val  = root[g_cid];
        Serial.println(val);
        if (atol(val) == 0) {
          digitalWrite(CONTROL_PIN, LOW);
        } else {
          digitalWrite(CONTROL_PIN, HIGH);  
        }
      }
    }

  } else {
    Serial.println("Client disconnected.");
    delay(5000);
    WiFi.disconnect();
    start_wsc();
  }

  // wait to fully let the client disconnect
  delay(100);
}
/* web socket client end */

/* acress point start */
void start_ac() {
  Serial.println("start acess point");
  app_mode = MODE_AP_WS;
  
  WiFi.softAP(ssid_name);

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  
  server.on("/", ac_handle_root);
  server.on("/s", ac_handle_set);
  
  server.begin();

  digitalWrite(STATUS_PIN, HIGH);
}

void ac_handle_root() {
  //server.send(200, "text/html", channel_name + ";" + channel_type);
  String output = channel_name + ";" + channel_type;
  server.send(200, "text/plain", output);
}

void ac_handle_set() {
  String ssid = "";
  String password = "";
  String cid = "";
  String host = "";
  
  for (uint8_t i=0; i < server.args(); i++){
    if (server.argName(i) == "ssid") {
      ssid = server.arg(i);
    }
    if (server.argName(i) == "password") {
      password = server.arg(i);
    }
    if (server.argName(i) == "cid") {
      cid = server.arg(i);
    }
    if (server.argName(i) == "host") {
      host = server.arg(i);
    }
  }

  Serial.print("w:ssid: "); Serial.println(ssid);
  Serial.print("w:password: "); Serial.println(password);
  Serial.print("w:cid: "); Serial.println(cid);
  Serial.print("w:host: "); Serial.println(host);

  clean_eeprom(512);
  if (ssid.length() > 0) {
    write_eeprom(0, "1");
    write_eeprom(EEPROM_CHECK, ssid);
    write_eeprom(EEPROM_SSID, password);
    write_eeprom(EEPROM_CID, host);
  }
  if (cid.length() == 19) {
    write_eeprom(EEPROM_PASS, cid);
  }
  commit_eeprom();
  
  server.send(200, "text/plain", "success s: " + ssid + "; p = " + password + "; c = " + cid);
}
/* acress point end */

/* web server start */
void start_ws(String ssid, String password) {
  Serial.println("start web server");
  
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(250); digitalWrite(STATUS_PIN, HIGH);
    delay(250); digitalWrite(STATUS_PIN, LOW);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", ws_handle_root);

  digitalWrite(STATUS_PIN, HIGH);
  server.begin();
}

void ws_handle_root() {
  digitalWrite(CONTROL_PIN, HIGH);
  delay(1000);
  digitalWrite(CONTROL_PIN, LOW);
  
  server.send(200, "text/plain", "web server started");
}
/* web server end */

/* reset start */
void check_reset_device() {
  int reset = digitalRead(RESET_PIN);
  if (reset == HIGH && !restarted) {
    digitalWrite(SIGNAL_PIN, HIGH);
    restarted = true;
    clean_eeprom(512);
    EEPROM.end();
    commit_eeprom();
    Serial.println("clean eeprom successful");
    while(1) { }
  }
}
/* reset end */

void loop() {
  check_reset_device();

  if(app_mode == MODE_WSC) {
    wsc_handle();  
  } else if(app_mode == MODE_AP_WS) {
    server.handleClient();
  }
}

/* ---------------------------------------------------------------- */

/* eeprom start */
String write_eeprom(int start, String s) {
  for (int i = 0; i < s.length(); ++i) {
    EEPROM.write(start + i, s[i]);
  }
}

String read_eeprom(int start, int end) {
  String s;
  for (int i = start; i < end; ++i) {
     s+= char(EEPROM.read(i));
  }
  return(s);
}

void clean_eeprom(int len) {
  for (int i = 0; i < len; i++) {
    EEPROM.write(i, 0);
  }
}

void commit_eeprom(void) {
  EEPROM.commit();
}
/* eeprom end */
