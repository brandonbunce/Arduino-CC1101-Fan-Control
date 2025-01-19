#include "Arduino_LED_Matrix.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <WiFiS3.h>
#include <PubSubClient.h>
#include "credentials.h"

#define MQTT_CLIENT_NAME "arduinofan"
#define BASE_TOPIC "home/arduinofan"

// Define MQTT topics
#define STATUS_TOPIC BASE_TOPIC "/status"
#define SUBSCRIBE_TOPIC_ON_SET BASE_TOPIC "/+/on/set"
#define SUBSCRIBE_TOPIC_ON_STATE BASE_TOPIC "/+/on/state"
#define SUBSCRIBE_TOPIC_SPEED_SET BASE_TOPIC "/+/speed/set"
#define SUBSCRIBE_TOPIC_SPEED_STATE BASE_TOPIC "/+/speed/state"
#define SUBSCRIBE_TOPIC_LIGHT_SET BASE_TOPIC "/+/light/set"
#define SUBSCRIBE_TOPIC_LIGHT_STATE BASE_TOPIC "/+/light/state"
#define SUBSCRIBE_TOPIC_DIRECTION_SET BASE_TOPIC "/+/direction/set"
#define SUBSCRIBE_TOPIC_DIRECTION_STATE BASE_TOPIC "/+/direction/state"

// Define CC1101 settings
#define CC1101_RX_FREQUENCY 303.85  //mhz
#define CC1101_TX_FREQUENCY 303.85  //mhz

#pragma region LED_Matrix

// Made using https://ledmatrix-editor.arduino.cc/

unsigned long ledTransmit[] = {
  0x30c26426,
  0x430c0600,
  0x60060060
};

unsigned long ledReceive[] = {
  0x30c16816,
  0x830c0600,
  0x60060060
};

unsigned long ledIdle[] = {
  0x6006,
  0x600,
  0x60060060
};

unsigned long ledError[] = {
  0x19811,
  0x1f82,
  0x4204000,
};

#pragma endregion

// Instantiate objects
WiFiClient wifiClient;
PubSubClient client(wifiClient);
RCSwitch mySwitch = RCSwitch();
ArduinoLEDMatrix matrix;

// Instantiate variables
int wifiStatus = WL_IDLE_STATUS;

// Define fan structure.
struct fan {
  const char* fanIdentifier;  // This is the fan's 16-bit identifier (ex. 0111110111100011)
  bool lightState;            // Is the light on or off?
  bool fanState;              // Is the fan rotating?
  bool fanDirection;          // Is the fan blowing forward/down (true) or reverse/up (false)?
  uint8_t fanSpeed;           // What speed is the fan? (1-6)
  bool stateIsKnown;          // Does the controller know the state of this fan? (ex. did the fan state change while the controller lost power?)
};

const char* fanCommand[10] = {
  "10100000",  // Toggle Light
  "11000000",  // Speed 1
  "10011000",  // Speed 2
  "01000000",  // Speed 3
  "01010000",  // Speed 4
  "10010100",  // Speed 5
  "10000000",  // Speed 6
  "00100000",  // Stop
  "11010000",  // Reverse
  "11110000"   // Reset (Speed 0, Blowing Down, Light Off and Max Brightness)
};

fan fans[3] = { { "0111110111100011", false, false, true, 3, false }, { "1100100111101111", false, false, true, 3, false }, { "0100101111101101", false, false, true, 3, false } };
// We are initializing 3 elements. But remember, we address these 0-2.

// CC1101 to Arduino UNO R4 WiFi Map
// GND - GND
// VCC - 3.3v
// GD0 - GPIO 4 (Transmit)
// CSN - GPIO 10
// SCK - GPIO 13
// MOSI - GPIO 11
// MISO - GPIO 12
// GD02 - GPIO 2 (Receive)

void setup() {
  Serial.begin(9600);
  initializeDisplay();
  initializeRadio();
  initializeNetwork();
  initializeMQTT();
}

void loop() {

  // Handle incoming Serial commands.
  if (Serial.available()) {
    String serialCommand;
    int selectedFan;
    serialCommand = Serial.readStringUntil('\n');
    serialCommand.toLowerCase();
    serialCommand.trim();
    if (serialCommand.equals("selectfan")) {
      Serial.println("Please select fan (1-3):");
      int argument1 = NULL;
      while (!argument1) {
        argument1 = Serial.readStringUntil('\n').toInt();
      }
      if (argument1 <= 3 && argument1 > 0) {
        selectedFan = argument1;
        Serial.print("Selected fan: ");
        Serial.println(argument1);
      } else {
        Serial.println("Tried to select fan that does not exist. Please choose between 1-3.");
      }
    } else if (serialCommand.equals("speed")) {
      int argument1;
      Serial.println("Please select fan speed (1-6):");
      while (!argument1) {
        argument1 = Serial.readStringUntil('\n').toInt();
      }
      Serial.print("Selected fan speed: ");
      Serial.println(String(argument1));
      sendRadioCommand(assembleRadioCommand(selectedFan, argument1));
      updateFanState(selectedFan, argument1);

    } else if (serialCommand.equals("light")) {
      Serial.println("Toggling light on fan.");
      sendRadioCommand(assembleRadioCommand(selectedFan, 0));
      updateFanState(selectedFan, 0);
    } else if (serialCommand.equals("stop")) {
      Serial.println("Stopping fan.");
      sendRadioCommand(assembleRadioCommand(selectedFan, 7));
      updateFanState(selectedFan, 7);
    } else if (serialCommand.equals("reverse")) {
      Serial.println("Reversing fan.");
      sendRadioCommand(assembleRadioCommand(selectedFan, 8));
      updateFanState(selectedFan, 8);
    } else if (serialCommand.equals("sendbinary")) {
      // This will only send 24bit codes (+1 is terminator)
      char argument1[25];
      while ((strlen(argument1)) == 0) {
        // This will only send 24bit codes (+1 is terminator)
        Serial.readStringUntil('\n').toCharArray(argument1, 25);
      }
      sendRadioCommand(argument1);
    } else if (serialCommand.equals("sendint")) {

      int argument1;
      Serial.println("Please enter int:");
      while (!argument1) {
        argument1 = Serial.readStringUntil('\n').toInt();
      }
      Serial.println(argument1);

      static char radioCommand[25];                          // 24 bits + null terminator
      strcpy(radioCommand, fans[0].fanIdentifier);           // Copy the identifier to radioCommand
      strcat(radioCommand, dec2binWzerofill(argument1, 8));  // Concatenate the light command

      // Transmit command over radio.
      Serial.print("TX: ");
      matrix.loadFrame(ledTransmit);
      setRadioMode(true);
      delay(100);  // Maybe remove this? Added as precaution to prevent dropped commands in case radio takes time to switch over.
      mySwitch.send(radioCommand);
      setRadioMode(false);
      matrix.loadFrame(ledIdle);
    } else if (serialCommand.equals("mqtton")) {
      client.publish("home/arduinofan/1/speed/state", "4", true);
      client.publish("home/arduinofan/1/on/state", "ON", true);
      client.publish("home/arduinofan/1/light/state", "ON", true);
      //client.publish("home/arduinofan/1/direction/state", "1", true);
    } else if (serialCommand.equals("mqttspeed")) {
      client.publish("home/arduinofan/1/speed/state", "6", true);
      //client.publish("home/arduinofan/1/direction/state", "1", true);
    } else if (serialCommand.equals("mqttoff")) {
      client.publish("home/arduinofan/1/light/state", "OFF", true);
      client.publish("home/arduinofan/1/speed/state", "1", true);
      client.publish("home/arduinofan/1/on/state", "OFF", true);
    } else if (serialCommand.equals("help")) {
      Serial.println("selectfan (1-3) - Select target fan.");
      Serial.println("light - Toggle fan light.");
      Serial.println("speed (1-6) - Change fan speed.");
      Serial.println("stop - Stop fan.");
      Serial.println("reverse - Reverse fan.");
      Serial.println("sendbinary - Send a custom 24-bit binary sequence.");
    } else {
      Serial.println("Invalid command!");
    }
  }

  // Handle incoming commands from radio.
  if (mySwitch.available()) {
    matrix.loadFrame(ledReceive);
    char* binary = dec2binWzerofill(mySwitch.getReceivedValue(), 24);
    Serial.print("RX: ");
    Serial.println(binary);
    decodeCommand(binary);
    mySwitch.resetAvailable();
    matrix.loadFrame(ledIdle);
  }

  // Check if we are still (or were ever) connected to MQTT
  if (!client.connected()) {
    initializeMQTT();
  }

  // Check for any incoming commands from MQTT
  client.loop();
}

// Set up radio for transmission.
void initializeRadio() {
  while (!ELECHOUSE_cc1101.getCC1101()) {
    Serial.println("CC1101: SPI communication failure! Please check connections and try again. Trying again in 5 seconds.");
    delay(5000);
  }
  // Driver initializes radio thru SPI.
  ELECHOUSE_cc1101.Init();
  // Driver sets radio to ASK/OOK mode.
  ELECHOUSE_cc1101.setModulation(2);
  // Set protocol.
  //mySwitch.setProtocol(11); // HT12E Encoder
  mySwitch.setProtocol(6);  // Might actually be protocol 6? (HT6P20B Encoder)
  // Repeat transmission about 15 times, emulating a normal controller press.
  mySwitch.setRepeatTransmit(15);
  // Set pulse length to 290 microseconds.
  mySwitch.setPulseLength(290);
  // Set radio to receive.
  setRadioMode(false);
  // Done!
}

void initializeNetwork() {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // Do not continue.
    matrix.loadFrame(ledError);
    while (true)
      ;
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the ESP32 firmware.");
  }

  // attempt to connect to WiFi network:
  while (wifiStatus != WL_CONNECTED) {
    matrix.loadFrame(ledIdle);
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(WIFI_SSID);  // print the network name (SSID);

    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifiStatus = WiFi.begin(WIFI_SSID, WIFI_PASS);
    // wait 10 seconds for connection:
    delay(10000);
  }
  printWifiStatus();  // you're connected now, so print out the status
}

void initializeDisplay() {
  matrix.begin();
  matrix.loadFrame(ledIdle);
}

void setRadioMode(bool enableTransmit) {
  // We are transmitting.
  if (enableTransmit) {
    // Driver sets radio frequency thru SPI.
    ELECHOUSE_cc1101.setMHZ(CC1101_TX_FREQUENCY);
    // Set hardware to transmit thru SPI.
    ELECHOUSE_cc1101.SetTx();
    // Set library to transmit.
    mySwitch.disableReceive();
    mySwitch.enableTransmit(4);
  }
  // We are listening.
  else {
    // Driver sets radio frequency thru SPI.
    ELECHOUSE_cc1101.setMHZ(CC1101_RX_FREQUENCY);
    // Set hardware to receive thru SPI.
    ELECHOUSE_cc1101.SetRx();
    // Set library to receive.
    mySwitch.disableTransmit();
    mySwitch.enableReceive(2);
  }
}

void initializeMQTT() {
  // Loop until we're reconnected
  // Now we can start MQTT broker connection
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(mqttCallback);
  Serial.print("MQTT: Attempting connection...");
  // Attempt to connect
  if (client.connect(MQTT_CLIENT_NAME, MQTT_USER, MQTT_PASS, STATUS_TOPIC, 0, true, "offline")) {
    Serial.println("MQTT: Connection established to broker.");
    // Once connected, publish an announcement...
    client.publish(STATUS_TOPIC, "online", true);
    // ... and resubscribe
    client.subscribe(SUBSCRIBE_TOPIC_ON_SET);
    client.subscribe(SUBSCRIBE_TOPIC_ON_STATE);
    client.subscribe(SUBSCRIBE_TOPIC_SPEED_SET);
    client.subscribe(SUBSCRIBE_TOPIC_SPEED_STATE);
    client.subscribe(SUBSCRIBE_TOPIC_LIGHT_SET);
    client.subscribe(SUBSCRIBE_TOPIC_LIGHT_STATE);
    client.subscribe(SUBSCRIBE_TOPIC_DIRECTION_STATE);
    client.subscribe(SUBSCRIBE_TOPIC_DIRECTION_SET);
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
    // Wait 5 seconds before retrying
    delay(5000);
  }
}

void decodeCommand(char* binary) {
  int fan = NULL;
  int command = NULL;
  // Extract the first 16 bits for the fan identifier
  char receivedFanId[17];              // 16 bits + 1 for null terminator
  strncpy(receivedFanId, binary, 16);  // Copy the first 16 bits
  receivedFanId[16] = '\0';            // Null terminate
  // [16] IS the last value in array, as we are working 0-indexed

  //Serial.println(String(receivedFanId));

  // Extract the last 8 bits for the command
  char receivedCommand[9];                   // 8 bits + 1 for null terminator
  strncpy(receivedCommand, binary + 16, 8);  // Copy the last 8 bits
  receivedCommand[8] = '\0';                 // Null terminate

  //Serial.println(String(receivedCommand));

  // Match command first
  for (int i = 0; i <= 8; i++) {
    if (strcmp(fanCommand[i], receivedCommand) == 0) {
      command = i;
      break;
    }
  }

  // Then, match fan and make sure its ours, then we can execute an update of our records.
  for (int i = 0; i <= 2; i++) {
    if (strcmp(fans[i].fanIdentifier, receivedFanId) == 0) {
      fan = (i + 1);
      updateFanState(fan, command);
      break;
    } else if (i == 2) {
      Serial.print("Identifier ");
      Serial.print(receivedFanId);
      Serial.println(" does not belong to us!");
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("MQTT RX: [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  char payloadChar[length + 1];
  sprintf(payloadChar, "%s", payload);
  payloadChar[length] = '\0';

  // Get ID after the base topic + a slash
  char id[2];
  memcpy(id, &topic[strlen(BASE_TOPIC) + 1], 1);  // strlen(BASE_TOPIC) + 1 to skip the slash
  id[1] = '\0';

  uint8_t idint = strtol(id, (char**)NULL, 10);  // Use base 10 instead of base 2
  char* attr;
  char* action;
  // Split by slash after ID in topic to get attribute and action
  attr = strtok(topic + sizeof(BASE_TOPIC) + 2, "/");
  action = strtok(NULL, "/");
  // Convert payload to lowercase
  for (int i = 0; payloadChar[i]; i++) {
    payloadChar[i] = tolower(payloadChar[i]);
  }

  if (strcmp(action, "set") == 0) {
    if (strcmp(attr, "on") == 0) {
      if (strcmp(payloadChar, "on") == 0) {
        // Send last recorded speed to the fan.
        sendRadioCommand(assembleRadioCommand(idint, fans[idint - 1].fanSpeed));
        updateFanState(idint, fans[idint - 1].fanSpeed);
      } else if (strcmp(payloadChar, "off") == 0) {
        // Send stop command to fan.
        sendRadioCommand(assembleRadioCommand(idint, 7));
        updateFanState(idint, 7);
      }
    }
    if (strcmp(attr, "speed") == 0) {
      if (strcmp(payloadChar, "0") == 0) {
        // In the percentage slider, this looks like off, so we treat it as off.
        sendRadioCommand(assembleRadioCommand(idint, 7));
        updateFanState(idint, 7);
      } else if (strcmp(payloadChar, "1") == 0) {
        sendRadioCommand(assembleRadioCommand(idint, 1));
        updateFanState(idint, 1);
      } else if (strcmp(payloadChar, "2") == 0) {
        sendRadioCommand(assembleRadioCommand(idint, 2));
        updateFanState(idint, 2);
      } else if (strcmp(payloadChar, "3") == 0) {
        sendRadioCommand(assembleRadioCommand(idint, 3));
        updateFanState(idint, 3);
      } else if (strcmp(payloadChar, "4") == 0) {
        sendRadioCommand(assembleRadioCommand(idint, 4));
        updateFanState(idint, 4);
      } else if (strcmp(payloadChar, "5") == 0) {
        sendRadioCommand(assembleRadioCommand(idint, 5));
        updateFanState(idint, 5);
      } else if (strcmp(payloadChar, "6") == 0) {
        sendRadioCommand(assembleRadioCommand(idint, 6));
        updateFanState(idint, 6);
      }
    } else if (strcmp(attr, "light") == 0) {
      if (strcmp(payloadChar, "on") == 0) {
        // Only send on command if light is off.
        if (!fans[idint - 1].lightState) {
          sendRadioCommand(assembleRadioCommand(idint, 0));
          updateFanState(idint, 0);
        }
      } else if (strcmp(payloadChar, "off") == 0) {
        // Only send off command if light is on.
        if (fans[idint - 1].lightState) {
          sendRadioCommand(assembleRadioCommand(idint, 0));
          updateFanState(idint, 0);
        }
      }
    } else if (strcmp(attr, "direction") == 0) {
      if (strcmp(payloadChar, "forward") == 0) {
        // Only send flip fan command if backwards.
        if (!fans[idint - 1].fanDirection) {
          sendRadioCommand(assembleRadioCommand(idint, 8));
          updateFanState(idint, 8);
        }
      } else if (strcmp(payloadChar, "reverse") == 0) {
        // Only send flip fan command if forwards.
        if (fans[idint - 1].fanDirection) {
          sendRadioCommand(assembleRadioCommand(idint, 8));
          updateFanState(idint, 8);
        }
      }
    }
  }
}

void publishMQTTState(int fan) {
  char outTopic[100];
  char fanIDStr[2];
  char fanSpeedStr[4];
  itoa(fan, fanIDStr, 10);                        // Convert fan (integer) to a character array (base 10)
  itoa(fans[fan - 1].fanSpeed, fanSpeedStr, 10);  // Convert fanSpeed (uint8_t) to a character array (base 10)
  sprintf(outTopic, "%s/%s/on/state", BASE_TOPIC, fanIDStr);
  client.publish(outTopic, fans[fan - 1].fanState ? "ON" : "OFF", true);
  sprintf(outTopic, "%s/%s/speed/state", BASE_TOPIC, fanIDStr);
  client.publish(outTopic, fanSpeedStr, true);
  sprintf(outTopic, "%s/%s/light/state", BASE_TOPIC, fanIDStr);
  client.publish(outTopic, fans[fan - 1].lightState ? "ON" : "OFF", true);
  sprintf(outTopic, "%s/%s/direction/state", BASE_TOPIC, fanIDStr);
  client.publish(outTopic, fans[fan - 1].fanDirection ? "forward" : "reverse", true);
}

void updateFanState(int fan, int command) {
  switch (command) {
    case 0:  // Toggle light
      {
        fans[fan - 1].lightState = !fans[fan - 1].lightState;
        break;
      }
    case 1 ... 6:  // Set speed
      {
        fans[fan - 1].fanState = true;
        fans[fan - 1].fanSpeed = command;
        break;
      }
    case 7:  // Stop
      {
        fans[fan - 1].fanState = false;
        break;
      }
    case 8:  // Toggle direction
      {
        fans[fan - 1].fanDirection = !fans[fan - 1].fanDirection;
        break;
      }
    default:
      {
        Serial.print("ERROR: Tried to update fan with a command that doesn't exist.");
        break;
      }
  }
  printFanStatus(fan);
  publishMQTTState(fan);
}

void syncFan(int fan) {  // Not implemented
  // Send special command 11110000,
  // then, tell fan to be in the state we think it is.
  // Send reset command
  sendRadioCommand(assembleRadioCommand(fan, 9));
  delay(500);
  // Send speed
  sendRadioCommand(assembleRadioCommand(fan, fans[fan - 1].fanSpeed));
  delay(500);
  // If light is supposed to be on, turn it on.
  if (fans[fan - 1].lightState) {
    sendRadioCommand(assembleRadioCommand(fan, 0));
  }
}

char* assembleRadioCommand(int fan, int command) {
  //Serial.print("Called assembleCommand fan:");
  //Serial.println(fan);
  //Serial.print("Called assembleCommand command:");
  //Serial.println(command);

  if ((fan < 1 || fan > 3) || (command < 0 || command > 9)) {
    Serial.println("ERROR: Failed to assemble radio command.");
    Serial.print("Fan ");
    Serial.print(String(fan));
    Serial.print(" and command ");
    Serial.print(String(command));
    Serial.println(" are not valid parameters.");
    return NULL;  // Command failed, will not send.
  } else {
    char* identifier = NULL;
    char* commandByte = NULL;
    static char radioCommand[25];  // 24 bits + null terminator

    identifier = const_cast<char*>(fans[fan - 1].fanIdentifier);
    // We CANNOT modify this pointer as it is still the original "const" data casted onto a normal pointer, and will cause undefined behaviour.

    commandByte = const_cast<char*>(fanCommand[command]);
    // We CANNOT modify this pointer as it is still the original "const" data casted onto a normal pointer, and will cause undefined behaviour.

    strcpy(radioCommand, identifier);   // Copy the identifier to radioCommand
    strcat(radioCommand, commandByte);  // Concatenate the light command
    radioCommand[24] = '\0';            // Null terminate
    return radioCommand;
  }
}

void sendRadioCommand(char* radioCommand) {
  if (!radioCommand) {
    Serial.println("ERROR: Tried to send radio command with NULL value. Returning.");
    return;
  }
  // Transmit command over radio.
  Serial.print("TX: ");
  Serial.println(radioCommand);
  matrix.loadFrame(ledTransmit);
  setRadioMode(true);
  //delay(100);  // Maybe remove this? Added as precaution to prevent dropped commands in case radio takes time to switch over.
  mySwitch.send(radioCommand);
  setRadioMode(false);
  matrix.loadFrame(ledIdle);


  // Update fan state to reflect what we just did.
  //if (updateState) {
  //  updateFanState(fan, command);
  //}
}

void printFanStatus(int fan) {
  Serial.print("--- Fan ");
  Serial.print(fan);
  Serial.println(" Status ---");
  Serial.print("Identifier: ");
  Serial.println(fans[fan - 1].fanIdentifier);
  Serial.print("Light State: ");
  Serial.println(fans[fan - 1].lightState);
  Serial.print("Fan State: ");
  Serial.println(fans[fan - 1].fanState);
  Serial.print("Speed: ");
  Serial.println(fans[fan - 1].fanSpeed);
  Serial.print("Direction: ");
  if (fans[fan - 1].fanDirection) {
    Serial.println("Down");
  } else {
    Serial.println("Up");
  }
  Serial.print("State Sync: ");
  Serial.println(fans[fan - 1].stateIsKnown);
  Serial.println("--------------------");
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

// Function pulled from RCSwitch ReceiveDemo_Advanced.ino
static char* dec2binWzerofill(unsigned long Dec, unsigned int bitLength) {
  static char bin[64];
  unsigned int i = 0;

  while (Dec > 0) {
    bin[32 + i++] = ((Dec & 1) > 0) ? '1' : '0';
    Dec = Dec >> 1;
  }

  for (unsigned int j = 0; j < bitLength; j++) {
    if (j >= bitLength - i) {
      bin[j] = bin[31 + i - (j - (bitLength - i))];
    } else {
      bin[j] = '0';
    }
  }
  bin[bitLength] = '\0';

  return bin;
}
