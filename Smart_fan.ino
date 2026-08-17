//–––––––Libraries–––––––––––––––––––-
#include <Wire.h>
#include <WiFi.h>
#include <BLEUtils.h>
#include <IRremote.h>
#include <BLEServer.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WebSocketsServer.h>
//––––defining and declaring all the pins and objects–––––––––––––––––-

#define SERVICE_UUID "baf59357-e049-46e1-b4b9-57805c19e168"  //BLE UUID's
#define CHARACTERISTIC_UUID "3806cc92-126a-4458-b034-bfb752d73b21"
#define STATUS_UUID "d10fb985-512e-4c2c-8c4a-a1fe1aa74001"

BLEServer* bleserver = nullptr;
BLECharacteristic* CommandCharacteristic;
BLECharacteristic* StatusCharacteristic;
bool device_connected = false;

WebSocketsServer wsServer(81);  //declaring webserver and websocket
WebServer server(80);

const char* SSID = "YOUR_WIFI_SSID";  //wifi credentials
const char* password = "YOUR_WIFI_PASSWORD";

Adafruit_SH1106G display(128, 64, &Wire, -1);  //oled display

Servo xservo;
Servo yservo;

//All the pins have been defined locally for storage conservation

//All globally used variables
enum controls {
  joystick,
  remote,
  BLE,
  wifi,
  stop
};                                           //declaring an enum to indicate the state of the control
int xangle = 0, yangle = 90, increment = 3;  //increment is the angle the fan tilts with per increment
// increase the value of increment for faster movement
int dir = 0, speed = 255;  // a direction variable which makes the motor spin clockwise if its 0 anticlockwise otherwise
char cmd;
bool joy_button = false;
controls control = joystick;

//––––––Interrupt for stopping–––––––––––––
void on_button_press() {
  control = stop;
}
void joy_dir() {
  control = joystick;
  joy_button = true;
}


//–––––Control Managers–––––––––––––––––––
//this class of fucntions get activated according to hand of control and get the motor properties like speed,angles etc
void read_joystick(void* parametre) {
  while (true) {
    if (joy_button) {
      if (dir) dir = 0;
      else dir = 1;
      joy_button = false;
    }
    if (control == joystick) {
      speed = map(analogRead(33), 0, 4095, 0, 255);
    }
    if (analogRead(35) < 1600) {
      xangle -= increment;
      control = joystick;
    } else if (analogRead(35) > 2000) {
      xangle += increment;
      control = joystick;
    }
    if (analogRead(32) < 1600) {
      yangle += increment;
      control = joystick;
    } else if (analogRead(32) > 2000) {
      yangle -= increment;
      control = joystick;
    }
    xangle = constrain(xangle, 0, 180);
    yangle = constrain(yangle, 0, 180);

    vTaskDelay(pdMS_TO_TICKS(30));
  }
}  // gets control stats for joystick mode
void read_IR() {
  unsigned long value = IrReceiver.decodedIRData.decodedRawData;
  if (value == 0x0) {
    return;
  }
  switch (value) {
    case (0xE619FF00):
      cmd = '0';
      control = stop;
      break;
    case (0xBA45FF00):
      cmd = '1';
      speed = ((1 * 155) / 8) + 80;
      break;
    case (0xB946FF00):
      cmd = '2';
      speed = ((2 * 155) / 8) + 80;
      break;
    case (0xB847FF00):
      cmd = '3';
      speed = ((3 * 155) / 8) + 80;
      break;
    case (0xBB44FF00):
      cmd = '4';
      speed = ((4 * 155) / 8) + 80;
      break;
    case (0xBF40FF00):
      cmd = '5';
      speed = ((5 * 155) / 8) + 80;
      break;
    case (0xBC43FF00):
      cmd = '6';
      speed = ((6 * 155) / 8) + 80;
      break;
    case (0xF807FF00):
      cmd = '7';
      speed = ((7 * 155) / 8) + 80;
      break;
    case (0xEA15FF00):
      cmd = '8';
      speed = ((8 * 155) / 8) + 80;
      break;
    case (0xF609FF00):
      cmd = '9';
      speed = ((9 * 155) / 8) + 80;
      break;
    case (0xE916FF00):
      cmd = '*';
      break;
    case (0xF20DFF00):
      cmd = '#';
      break;
    case (0xE718FF00):
      cmd = 'u';
      yangle -= increment;
      break;
    case (0xF708FF00):
      cmd = '<';
      xangle -= increment;
      break;
    case (0xE31CFF00):
      cmd = 'o';
      if (dir) dir = 0;
      else dir = 1;
      break;
    case (0xA55AFF00):
      cmd = '>';
      xangle += increment;
      break;
    case (0xAD52FF00):
      cmd = 'd';
      yangle += increment;
      break;
    default:
      cmd = '_';
      break;
  }
  Serial.println(IrReceiver.decodedIRData.decodedRawData, HEX);
  xangle = constrain(xangle, 0, 180);
  yangle = constrain(yangle, 0, 180);
  vTaskDelay(pdMS_TO_TICKS(50));
}  //gets control stats for IR remote mode
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    device_connected = true;
    Serial.println("device connected!");
  }
  void onDisconnect(BLEServer* server) override {
    device_connected = false;
    Serial.println("Device disconnected");
  }
};  //call back that responds according to the connection status of the BLE device
void sendBLEStatus() {
  if (!device_connected) {
    return;
  }
  int percent = map(speed, 0, 255, 0, 100);
  String status =
    "SPEED:" + String(percent) + ",X:" + String(xangle) + ",Y:" + String(yangle) + ",MODE:" + String((int)control);
  StatusCharacteristic->setValue(status.c_str());
  StatusCharacteristic->notify();
}  //prints to the connected device the current stats
class MotorCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    String BLEcmd = String(characteristic->getValue().c_str());
    BLEcmd.trim();
    BLEcmd.toUpperCase();
    Serial.print("BLE command: ");
    Serial.println(BLEcmd);
    control = BLE;
    if (BLEcmd == "ON") {
      speed = 255;
    } else if (BLEcmd == "OFF") {
      control = stop;
    } else if (BLEcmd.startsWith("SPEED:")) {
      int percent = BLEcmd.substring(6).toInt();
      percent = constrain(percent, 0, 100);
      speed = map(percent, 0, 100, 0, 255);
    } else if (BLEcmd == "LEFT") {
      xangle -= increment;
    } else if (BLEcmd == "RIGHT") {
      xangle += increment;
    } else if (BLEcmd == "UP") {
      yangle -= increment;
    } else if (BLEcmd == "DOWN") {
      yangle += increment;
    } else if (BLEcmd == "REVERSE") {
      if (dir) dir = 0;
      else dir = 1;
    }
    xangle = constrain(xangle, 0, 180);
    yangle = constrain(yangle, 0, 180);
    sendBLEStatus();
  }
};  //control stats are updated by this fucntion whenever the BLE sends any command
void onWebSocketEvent(uint8_t clientNum, WStype_t type,
                      uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("WiFi client %d connected\n", clientNum);
  }
  if (type == WStype_DISCONNECTED) {
    Serial.printf("WiFi client %d disconnected\n", clientNum);
    // If WiFi was in control, release it back to joystick
    if (control == wifi) control = joystick;
  }
  if (type == WStype_TEXT) {
    String msg = String((char*)payload);
    msg.trim();
    msg.toUpperCase();
    Serial.println("WiFi cmd: " + msg);

    control = wifi;   // WiFi takes control the moment it sends anything

    if (msg == "ON") {
      speed = 255;
    } else if (msg == "OFF") {
      control = stop;
    } else if (msg == "REVERSE") {
      dir = !dir;
    } else if (msg.startsWith("SPEED:")) {
      int percent = constrain(msg.substring(6).toInt(), 0, 100);
      speed = map(percent, 0, 100, 0, 255);
    } else if (msg.startsWith("X:")) {
      xangle = constrain(msg.substring(2).toInt(), 0, 180);
    } else if (msg.startsWith("Y:")) {
      yangle = constrain(msg.substring(2).toInt(), 0, 180);
    } else if (msg == "JOYSTICK") {
      control = joystick;   // hand control back to joystick
    }

    // Push updated status back to browser immediately
    String controlName;
    switch(control) {
      case joystick: controlName = "JOYSTICK"; break;
      case remote:   controlName = "REMOTE";   break;
      case BLE:      controlName = "BLE";      break;
      case wifi:     controlName = "WIFI";     break;
      case stop:     controlName = "STOP";     break;
    }
    int percent = map(speed, 0, 255, 0, 100);
    String status = "{\"speed\":" + String(percent) +
                    ",\"x\":"     + String(xangle)  +
                    ",\"y\":"     + String(yangle)  +
                    ",\"dir\":"   + String(dir)      +
                    ",\"mode\":\"" + controlName + "\"}";
    wsServer.broadcastTXT(status);
  }
} //websocket handler for the wifi control

//––––––WiFi control website script and the handler fucntion–––––––––––
  const char* dashboardHTML = R"rawhtml(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Motor Control</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        * { box-sizing:border-box; margin:0; padding:0; }
        body {
          font-family:sans-serif; background:#0d1117;
          color:#c9d1d9; padding:20px;
          display:flex; flex-direction:column; align-items:center; gap:14px;
        }
        h1 { color:#58a6ff; font-size:20px; }

        .status-bar {
          width:100%; max-width:500px; background:#161b22;
          border:1px solid #30363d; border-radius:10px;
          padding:12px 16px; display:grid;
          grid-template-columns:repeat(2,1fr); gap:8px;
        }
        .stat { text-align:center; }
        .stat-label { font-size:10px; color:#8b949e; text-transform:uppercase; }
        .stat-value { font-size:22px; font-weight:bold; color:#3fb950; }

        .mode-badge {
          padding:8px 20px; border-radius:20px; font-size:13px;
          font-weight:500; background:#21262d; color:#58a6ff;
          border:1px solid #58a6ff; text-align:center;
        }

        .card {
          width:100%; max-width:500px; background:#161b22;
          border:1px solid #30363d; border-radius:10px; padding:16px;
        }
        .card h3 { font-size:13px; color:#8b949e; margin-bottom:12px;
                   text-transform:uppercase; letter-spacing:.05em; }

        .btn-grid { display:grid; grid-template-columns:1fr 1fr; gap:10px; }
        .btn {
          padding:12px; border:none; border-radius:8px;
          font-size:15px; font-weight:500; cursor:pointer;
        }
        .btn-on      { background:#238636; color:white; }
        .btn-off     { background:#da3633; color:white; }
        .btn-rev     { background:#1f6feb; color:white; }
        .btn-joy     { background:#6e40c9; color:white; }
        .btn-dir     {
          padding:14px; border:none; border-radius:8px;
          background:#21262d; color:#c9d1d9; font-size:18px; cursor:pointer;
        }

        .dpad {
          display:grid; grid-template-columns:repeat(3,1fr);
          gap:8px; max-width:200px; margin:0 auto;
        }
        .dpad-empty { background:transparent; pointer-events:none; }

        .slider-row { display:flex; align-items:center; gap:10px; }
        .slider-row label { font-size:12px; color:#8b949e; min-width:60px; }
        input[type=range] { flex:1; accent-color:#58a6ff; }
        .slider-val { font-size:13px; font-weight:500; color:#3fb950; min-width:36px; text-align:right; }

        .ws-dot { width:8px; height:8px; border-radius:50%;
                  background:#da3633; display:inline-block; margin-right:6px; }
        .ws-dot.ok { background:#3fb950; }
        .ws-status { font-size:12px; color:#8b949e; }
      </style>
    </head>
    <body>
      <h1>Motor Control Panel</h1>

      <!-- Live status -->
      <div class="status-bar">
        <div class="stat">
          <div class="stat-label">Speed</div>
          <div class="stat-value" id="spd">--</div>
        </div>
        <div class="stat">
          <div class="stat-label">Direction</div>
          <div class="stat-value" id="dirVal">--</div>
        </div>
        <div class="stat">
          <div class="stat-label">X angle</div>
          <div class="stat-value" id="xVal">--</div>
        </div>
        <div class="stat">
          <div class="stat-label">Y angle</div>
          <div class="stat-value" id="yVal">--</div>
        </div>
      </div>

      <div class="mode-badge" id="mode">Connecting...</div>

      <!-- Motor control -->
      <div class="card">
        <h3>Motor</h3>
        <div class="btn-grid">
          <button class="btn btn-on"  onclick="send('ON')">ON</button>
          <button class="btn btn-off" onclick="send('OFF')">OFF</button>
          <button class="btn btn-rev" onclick="send('REVERSE')">Reverse</button>
          <button class="btn btn-joy" onclick="send('JOYSTICK')">Hand to Joystick</button>
        </div>
        <br>
        <div class="slider-row">
          <label>Speed</label>
          <input type="range" min="0" max="100" value="100" id="speedSlider"
            oninput="document.getElementById('spdLabel').textContent=this.value+'%'"
            onchange="send('SPEED:'+this.value)">
          <span class="slider-val" id="spdLabel">100%</span>
        </div>
      </div>

      <!-- Servo control -->
      <div class="card">
        <h3>Servo Pan / Tilt</h3>
        <div class="slider-row">
          <label>X (Pan)</label>
          <input type="range" min="0" max="180" value="90" id="xSlider"
            oninput="document.getElementById('xLabel').textContent=this.value+'°'"
            onchange="send('X:'+this.value)">
          <span class="slider-val" id="xLabel">90°</span>
        </div>
        <br>
        <div class="slider-row">
          <label>Y (Tilt)</label>
          <input type="range" min="0" max="180" value="90" id="ySlider"
            oninput="document.getElementById('yLabel').textContent=this.value+'°'"
            onchange="send('Y:'+this.value)">
          <span class="slider-val" id="yLabel">90°</span>
        </div>
        <br>
        <!-- D-pad for fine control -->
        <div class="dpad">
          <div></div>
          <button class="btn-dir" onclick="send('Y:'+Math.max(0,  parseInt(document.getElementById('ySlider').value)-5))  ">▲</ button>
          <div></div>
          <button class="btn-dir" onclick="send('X:'+Math.max(0,  parseInt(document.getElementById('xSlider').value)-5))  ">◀</ button>
          <div></div>
          <button class="btn-dir" onclick="send('X:'+Math.min(180,parseInt(document.getElementById('xSlider').value)+5))  ">▶</ button>
          <div></div>
          <button class="btn-dir" onclick="send('Y:'+Math.min(180,parseInt(document.getElementById('ySlider').value)+5))  ">▼</ button>
          <div></div>
        </div>
      </div>

      <!-- Connection status -->
      <p class="ws-status"><span class="ws-dot" id="dot"></span><span id="wsMsg">Connecting...</span></p>

      <script>
        const ws = new WebSocket('ws://' + window.location.hostname + ':81');

        function send(cmd) {
          if (ws.readyState === WebSocket.OPEN) ws.send(cmd);
        }

        ws.onopen = function() {
          document.getElementById('dot').className = 'ws-dot ok';
          document.getElementById('wsMsg').textContent = 'Connected - live updates active';
          document.getElementById('mode').textContent = 'WIFI';
        };

        ws.onclose = function() {
          document.getElementById('dot').className = 'ws-dot';
          document.getElementById('wsMsg').textContent = 'Disconnected - refresh to reconnect';
        };

        ws.onmessage = function(event) {
          const d = JSON.parse(event.data);

          document.getElementById('spd').textContent    = d.speed + '%';
          document.getElementById('dirVal').textContent  = d.dir ? 'CCW' : 'CW';
          document.getElementById('xVal').textContent   = d.x + '°';
          document.getElementById('yVal').textContent   = d.y + '°';
          document.getElementById('mode').textContent   = d.mode;

          // Sync sliders with ESP32 state
          document.getElementById('speedSlider').value = d.speed;
          document.getElementById('spdLabel').textContent = d.speed + '%';
          document.getElementById('xSlider').value = d.x;
          document.getElementById('xLabel').textContent = d.x + '°';
          document.getElementById('ySlider').value = d.y;
          document.getElementById('yLabel').textContent = d.y + '°';
        };
      </script>
    </body>
    </html>
    )rawhtml";

  void wifiTask(void* parameter) {
    server.on("/", []() {
      server.send(200, "text/html", dashboardHTML);
    });
    server.begin();

    wsServer.begin();
    wsServer.onEvent(onWebSocketEvent);

    Serial.println("Dashboard ready.");

    unsigned long lastBroadcast = 0;

    while (true) {
      server.handleClient();
      wsServer.loop();

      // Push status to browser every 300ms even without commands
      // so sliders stay in sync when IR or joystick changes things
      if (millis() - lastBroadcast >= 300) {
        lastBroadcast = millis();

        String controlName;
        switch(control) {
          case joystick: controlName = "JOYSTICK"; break;
          case remote:   controlName = "REMOTE";   break;
          case BLE:      controlName = "BLE";      break;
          case wifi:     controlName = "WIFI";     break;
          case stop:     controlName = "STOP";     break;
        }

        int percent = map(speed, 0, 255, 0, 100);
        String status = "{\"speed\":" + String(percent)   +
                        ",\"x\":"     + String(xangle)    +
                        ",\"y\":"     + String(yangle)    +
                        ",\"dir\":"   + String(dir)        +
                        ",\"mode\":\"" + controlName + "\"}";
        wsServer.broadcastTXT(status);
      }

      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }  //handler funtion 
//––––––Command Processors––––––––––––––
//this class of funtions are executed continously in the freeRTOS after getting the control stats from the above
void motor_task(void* parametre) {
  while (true) {
    if (control != stop) {
      if (dir) {
        digitalWrite(14, HIGH);
        digitalWrite(12, LOW);
      } else {
        digitalWrite(14, LOW);
        digitalWrite(12, HIGH);
      }
      analogWrite(27, speed);
    } else {
      analogWrite(27, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}  //controls the motor like speed and direction
void servo_task(void* parametre) {
  while (true) {
    xservo.write(xangle);
    yservo.write(yangle);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}  //controls the servos for the given x and y angles
void led_task(void* parametre) {
  while (true) {
    switch (control) {
      case (joystick):
        digitalWrite(15, HIGH);
        digitalWrite(2, HIGH);
        digitalWrite(4, LOW);
        break;
      case (remote):
        digitalWrite(15, HIGH);
        digitalWrite(2, LOW);
        digitalWrite(4, HIGH);
        break;
      case (BLE):
        digitalWrite(15, LOW);
        digitalWrite(2, LOW);
        digitalWrite(4, HIGH);
        break;
      case (wifi):
        digitalWrite(15, LOW);
        digitalWrite(2, HIGH);
        digitalWrite(4, HIGH);
        break;
      case (stop):
        digitalWrite(15, HIGH);
        digitalWrite(2, LOW);
        digitalWrite(4, LOW);
        break;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}  //controls the led indication according to the mode of control
void display_task(void* parametre) {
  while (true) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.print("Speed: ");
    display.print((speed * 100) / 255);
    display.println("%");

    display.print("X:");
    display.print(xangle);
    display.print("|| Y:");
    display.println(yangle);

    display.print("Control Mode:");
    display.println(String(control));
    display.display();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(15, OUTPUT);
  pinMode(2, OUTPUT);
  pinMode(4, OUTPUT);
  pinMode(36, INPUT);
  pinMode(35, INPUT);
  pinMode(32, INPUT);
  pinMode(13, INPUT_PULLUP);
  pinMode(33, INPUT);
  pinMode(27, OUTPUT);
  pinMode(14, OUTPUT);
  pinMode(12, OUTPUT);
  pinMode(26, OUTPUT);
  pinMode(25, OUTPUT);

  //display setup
  Wire.begin();
  display.begin(0x3c, true);
  display.clearDisplay();
  display.display();

  //Servo setup
  xservo.attach(26);
  yservo.attach(25);

  //Ir remote setup
  IrReceiver.begin(19, ENABLE_LED_FEEDBACK);

  //WiFi setup
  Serial.print("Connecting...");
  WiFi.begin(SSID, password);
  int attempts = 1;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    Serial.print(".");
    attempts++;
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n Connected!");
    Serial.println("Go to the Website: http://" + WiFi.localIP().toString());
  } else {
    Serial.println("WiFi was not able to connect");
  }

  //Interrupt for the button
  attachInterrupt(36, on_button_press, FALLING);
  attachInterrupt(13, joy_dir, FALLING);

  //BLE setup
  BLEDevice::init("ESP32");
  bleserver = BLEDevice::createServer();
  bleserver->setCallbacks(new ServerCallbacks());
  BLEService* service = bleserver->createService(SERVICE_UUID);
  BLECharacteristic* motorchar = service->createCharacteristic(CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ);
  motorchar->setCallbacks(new MotorCallbacks());
  motorchar->setValue("OFF");
  StatusCharacteristic = service->createCharacteristic(STATUS_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  service->start();
  BLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
  BLEDevice::startAdvertising();
  Serial.println("BLE server started , open nrf and connect");
  Serial.println("Commands :ON or OFF,SPEED:(any percentage of speed you want),LEFT,RIGHT,UP,DOWN,REVERSE");

  //Tasks to core
  xTaskCreatePinnedToCore(motor_task, "motor", 1024, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(servo_task, "servo", 1024, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(read_joystick, "joystick", 1024, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(led_task, "led", 1024, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(wifiTask, "wifi", 8192, NULL, 2, NULL, 0);
}
void loop() {
  if (IrReceiver.decode()) {
    control = remote;
    read_IR();
    IrReceiver.resume();
  }
}
