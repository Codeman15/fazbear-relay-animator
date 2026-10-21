/*
 * Fazbear Relay Animator - Stage ESP32 firmware starter
 * Requires ESP32 Arduino core and ArduinoJson library.
 * Change Wi-Fi credentials and API_TOKEN before use.
 * Test with all external relay loads disconnected.
 */
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

const char* WIFI_SSID = "CHANGE_ME_WIFI";
const char* WIFI_PASSWORD = "CHANGE_ME_PASSWORD";
const char* API_TOKEN = "CHANGE_ME_LONG_RANDOM_TOKEN";
const bool RELAY_ACTIVE_LOW = false; // Verify jumper/input polarity before changing.
const uint8_t RELAY_PINS[8] = {16, 17, 18, 19, 21, 22, 23, 25};

WebServer server(80);
bool relayState[8] = {false,false,false,false,false,false,false,false};
bool playing = false;
String activeSequenceId = "";
String activeSequenceJson = "";
uint32_t playStartMs = 0;
uint32_t lastEventMs = 0;
DynamicJsonDocument sequenceDoc(32768);

String sequencePath(const String& id) {
  String safe = "";
  for (size_t i=0; i<id.length() && i<48; i++) {
    char c = id[i];
    if (isalnum((unsigned char)c) || c=='-' || c=='_') safe += c;
  }
  return "/seq_" + safe + ".json";
}
void setRelay(uint8_t index, bool on) {
  if (index >= 8) return;
  relayState[index] = on;
  bool electricalHigh = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(RELAY_PINS[index], electricalHigh ? HIGH : LOW);
}
void allOff() { for (uint8_t i=0; i<8; i++) setRelay(i, false); }
bool authorized() {
  if (!server.hasHeader("X-Fazbear-Token")) return false;
  return server.header("X-Fazbear-Token") == String(API_TOKEN);
}
void sendJson(int code, JsonDocument& doc) {
  String out; serializeJson(doc, out); server.send(code, "application/json", out);
}
void sendError(int code, const char* msg) {
  DynamicJsonDocument d(256); d["ok"] = false; d["error"] = msg; sendJson(code, d);
}
bool readBody(DynamicJsonDocument& doc) {
  if (!server.hasArg("plain")) return false;
  return deserializeJson(doc, server.arg("plain")) == DeserializationError::Ok;
}
void handleStatus() {
  DynamicJsonDocument d(1024);
  d["ok"] = true; d["ip"] = WiFi.localIP().toString();
  d["playing"] = playing; d["activeSequenceId"] = activeSequenceId;
  JsonArray a = d.createNestedArray("relays");
  for (int i=0;i<8;i++) a.add(relayState[i]);
  sendJson(200,d);
}
void handleSequences() {
  DynamicJsonDocument d(4096); d["ok"] = true;
  JsonArray a = d.createNestedArray("sequences");
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  while (f) {
    String name = f.name();
    if (name.startsWith("/seq_") && name.endsWith(".json")) {
      DynamicJsonDocument s(32768);
      if (deserializeJson(s, f)) { f.close(); f=root.openNextFile(); continue; }
      JsonObject item = a.createNestedObject();
      item["id"] = s["id"] | "";
      item["name"] = s["name"] | "";
      item["durationMs"] = s["durationMs"] | 0;
    }
    f.close(); f = root.openNextFile();
  }
  sendJson(200,d);
}
void handleSaveSequence() {
  DynamicJsonDocument d(32768);
  if (!readBody(d)) { sendError(400,"Invalid JSON body"); return; }
  String id = d["id"] | "";
  if (id.length()==0 || !d["events"].is<JsonArray>()) { sendError(400,"Sequence requires id and events array"); return; }
  JsonArray events = d["events"].as<JsonArray>();
  if (events.size() > 1000) { sendError(400,"Too many events"); return; }
  for (JsonObject e : events) {
    int r = e["relay"] | 0;
    long t = e["timeMs"] | -1;
    if (r < 1 || r > 8 || t < 0 || !e["state"].is<bool>()) { sendError(400,"Invalid event; relay 1-8, timeMs >= 0, state boolean required"); return; }
  }
  String path = sequencePath(id);
  File f = LittleFS.open(path, "w");
  if (!f) { sendError(500,"Could not open sequence storage"); return; }
  serializeJson(d,f); f.close();
  DynamicJsonDocument out(256); out["ok"]=true; out["id"]=id; sendJson(200,out);
}
void handlePlaySequence() {
  String id = server.pathArg(0);
  File f = LittleFS.open(sequencePath(id), "r");
  if (!f) { sendError(404,"Sequence not found"); return; }
  activeSequenceJson = f.readString(); f.close();
  if (deserializeJson(sequenceDoc, activeSequenceJson)) { sendError(500,"Stored sequence JSON invalid"); return; }
  allOff();
  JsonArray initial = sequenceDoc["initialStates"].as<JsonArray>();
  for (int i=0; i<8 && i<(int)initial.size(); i++) setRelay(i, initial[i] | false);
  activeSequenceId = sequenceDoc["id"] | id;
  playStartMs = millis(); lastEventMs = 0; playing = true;
  DynamicJsonDocument d(256); d["ok"]=true; d["playing"]=true; d["id"]=activeSequenceId; sendJson(200,d);
}
void handleStop() { playing=false; activeSequenceId=""; allOff(); DynamicJsonDocument d(128); d["ok"]=true; d["stopped"]=true; sendJson(200,d); }
void handleRelay() {
  int n = server.pathArg(0).toInt();
  if (n < 1 || n > 8) { sendError(400,"Relay must be 1-8"); return; }
  DynamicJsonDocument d(256);
  if (!readBody(d) || !d["state"].is<bool>()) { sendError(400,"JSON state boolean required"); return; }
  playing=false; activeSequenceId="";
  setRelay(n-1, d["state"].as<bool>());
  DynamicJsonDocument out(128); out["ok"]=true; out["relay"]=n; out["state"]=relayState[n-1]; sendJson(200,out);
}
void handleShowEvent() {
  DynamicJsonDocument d(512);
  if (!readBody(d)) { sendError(400,"Invalid JSON body"); return; }
  String event = d["event"] | "";
  // Sequence-to-event mapping is intentionally explicit: name a sequence ID with
  // eventMap in your sequence JSON, or use a future mapping layer in the app.
  File root=LittleFS.open("/");
  File f=root.openNextFile();
  while(f) {
    String name=f.name();
    if(name.startsWith("/seq_") && name.endsWith(".json")) {
      DynamicJsonDocument s(32768);
      if(!deserializeJson(s,f) && String((const char*)(s["triggerEvent"] | ""))==event) {
        String id=s["id"] | ""; f.close();
        server.sendHeader("Location", "/api/v1/sequences/"+id+"/play");
        // Execute sequence directly instead of relying on redirect.
        File sf=LittleFS.open(sequencePath(id),"r");
        if(sf) { activeSequenceJson=sf.readString(); sf.close(); deserializeJson(sequenceDoc,activeSequenceJson); allOff(); activeSequenceId=id; playStartMs=millis(); lastEventMs=0; playing=true; }
        DynamicJsonDocument out(256); out["ok"]=true; out["event"]=event; out["sequenceId"]=id; sendJson(200,out); return;
      }
    }
    f.close(); f=root.openNextFile();
  }
  DynamicJsonDocument out(256); out["ok"]=true; out["event"]=event; out["matched"]=false; sendJson(200,out);
}
void setupRoutes() {
  const char* headers[] = {"X-Fazbear-Token"}; server.collectHeaders(headers,1);
  server.on("/api/v1/status", HTTP_GET, [](){ if(!authorized()){sendError(401,"Unauthorized");return;} handleStatus(); });
  server.on("/api/v1/sequences", HTTP_GET, [](){ if(!authorized()){sendError(401,"Unauthorized");return;} handleSequences(); });
  server.on("/api/v1/sequences", HTTP_PUT, [](){ if(!authorized()){sendError(401,"Unauthorized");return;} handleSaveSequence(); });
  server.on("/api/v1/sequences/stop", HTTP_POST, [](){ if(!authorized()){sendError(401,"Unauthorized");return;} handleStop(); });
  server.on("/api/v1/emergency-stop", HTTP_POST, [](){ if(!authorized()){sendError(401,"Unauthorized");return;} handleStop(); });
  server.on("/api/v1/relay/{n}", HTTP_POST, [](){ if(!authorized()){sendError(401,"Unauthorized");return;} handleRelay(); });
  server.on("/api/v1/show/event", HTTP_POST, [](){ if(!authorized()){sendError(401,"Unauthorized");return;} handleShowEvent(); });
  server.onNotFound([](){
    if(!authorized()){sendError(401,"Unauthorized");return;}
    String uri=server.uri();
    if(uri.startsWith("/api/v1/sequences/") && uri.endsWith("/play") && server.method()==HTTP_POST) {
      String middle=uri.substring(String("/api/v1/sequences/").length(), uri.length()-String("/play").length()-1);
      server.uri(); // Keep route parsing explicit below.
      File f=LittleFS.open(sequencePath(middle),"r");
      if(!f){sendError(404,"Sequence not found");return;}
      activeSequenceJson=f.readString(); f.close();
      if(deserializeJson(sequenceDoc,activeSequenceJson)){sendError(500,"Stored sequence invalid");return;}
      allOff(); JsonArray initial=sequenceDoc["initialStates"].as<JsonArray>();
      for(int i=0;i<8 && i<(int)initial.size();i++) setRelay(i,initial[i] | false);
      activeSequenceId=middle; playStartMs=millis(); lastEventMs=0; playing=true;
      DynamicJsonDocument out(256); out["ok"]=true; out["playing"]=true; out["id"]=middle; sendJson(200,out); return;
    }
    sendError(404,"Not found");
  });
}
void setup() {
  Serial.begin(115200);
  for(uint8_t i=0;i<8;i++){ pinMode(RELAY_PINS[i],OUTPUT); digitalWrite(RELAY_PINS[i], RELAY_ACTIVE_LOW ? HIGH : LOW); }
  allOff();
  if(!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  unsigned long start=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-start<20000){delay(300);Serial.print(".");}
  if(WiFi.status()==WL_CONNECTED) Serial.printf("\nStage relay API: http://%s\n",WiFi.localIP().toString().c_str());
  else Serial.println("\nWi-Fi failed; outputs remain OFF.");
  setupRoutes(); server.begin();
}
void loop() {
  server.handleClient();
  if(!playing) return;
  uint32_t elapsed=millis()-playStartMs;
  JsonArray events=sequenceDoc["events"].as<JsonArray>();
  for(JsonObject e:events) {
    uint32_t t=e["timeMs"] | 0;
    if(t<=elapsed && t>lastEventMs) {
      int r=e["relay"] | 0;
      if(r>=1 && r<=8) setRelay(r-1,e["state"] | false);
    }
  }
  lastEventMs=elapsed;
  uint32_t duration=sequenceDoc["durationMs"] | 0;
  if(duration>0 && elapsed>=duration) {
    playing=false; activeSequenceId="";
    String behavior=sequenceDoc["endBehavior"] | "ALL_OFF";
    if(behavior=="ALL_OFF") allOff();
  }
}
