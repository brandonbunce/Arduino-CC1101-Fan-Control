#include "Arduino_LED_Matrix.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RadioLib.h>
#include <RCSwitch.h>

ArduinoLEDMatrix matrix;

byte transmit[8][12] = { // Led matrix for on, and transmitting.
  { 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0 },
  { 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0 },
  { 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0 },
  { 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0 },
  { 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 }
};

byte off[8][12] = { // Led matrix for on, but not transmitting.
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 }
};

String serialCommand;
float frequency = 303.815;
int selectedFan;

RCSwitch mySwitch = RCSwitch();

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
  matrix.begin();
  matrix.renderBitmap(off, 8, 12);
  initializeRadio();
}

void initializeRadio() {
  // Driver initializes radio thru SPI.
  ELECHOUSE_cc1101.Init();
  // Driver sets radio frequency thru SPI.
  ELECHOUSE_cc1101.setMHZ(frequency);
  // Driver sets radio to transmit mode thru SPI.
  ELECHOUSE_cc1101.SetTx();
  // Driver sets radio to ASK/OOK mode.
  ELECHOUSE_cc1101.setModulation(2);
  // Disable receive just in case
  mySwitch.disableReceive();
  // Enable transmit in the library thru GPIO 4
  mySwitch.enableTransmit(4);
  // Set protocol to 11.
  mySwitch.setProtocol(11);
  // Repeat transmission about 15 times, emulating a normal controller press.
  mySwitch.setRepeatTransmit(15);
  // Set pulse length to 290 microseconds.
  mySwitch.setPulseLength(290);
}

void loop() {

  if (Serial.available()) {
    serialCommand = Serial.readStringUntil('\n');
    serialCommand.toLowerCase();
    serialCommand.trim();
    if (serialCommand.equals("selectfan")) 
    {
        Serial.println("Please select fan (1-3):");
        int argument1 = NULL;
        while (!argument1) 
        {
        argument1 = Serial.readStringUntil('\n').toInt();
        }
        if (argument1 <= 3 && argument1 > 0) 
        {
          selectedFan = argument1;
          Serial.print("Selected fan: ");
          Serial.println(argument1);
        }
        else 
        {
          Serial.println("Tried to select fan that does not exist. Please choose between 1-3.");
        }
    } 
    else if (serialCommand.equals("speed")) 
    {
      int argument1;
      Serial.println("Please select fan speed (1-6):");
      while (!argument1) 
      {
        argument1 = Serial.readStringUntil('\n').toInt();
      }
      Serial.print("Selected fan speed: ");
      Serial.println(String(argument1));
      transmitCommand(assembleCommand(selectedFan, argument1));

    } 
    else if (serialCommand.equals("light")) 
    {
      Serial.println("Toggling light on fan.");
      transmitCommand(assembleCommand(selectedFan, 0));
    } 
    else if (serialCommand.equals("stop")) 
    {
      Serial.println("Stopping fan.");
      transmitCommand(assembleCommand(selectedFan, 7));
    } 
    else if (serialCommand.equals("reverse")) 
    {
      Serial.println("Reversing fan.");
      transmitCommand(assembleCommand(selectedFan, 7));
    } 
    else if (serialCommand.equals("troll")) 
    {
      Serial.println("Trolling");
      troll();
    } 
    else if (serialCommand.equals("sendbinary")) 
    {
      // This will only send 24bit codes (+1 is terminator)
      char argument1[25];
      Serial.println((strlen(argument1)));
      while ((strlen(argument1)) == 0) 
      {
        // This will only send 24bit codes (+1 is terminator)
        Serial.readStringUntil('\n').toCharArray(argument1, 25);;
      }
      Serial.print("Sending binary code: ");
      Serial.println(argument1);
      mySwitch.send(argument1);
    } 
    else if (serialCommand.equals("help")) 
    {
      Serial.println("selectfan (1-3) - Select target fan.");
      Serial.println("light - Toggle fan light.");
      Serial.println("speed (1-6) - Change fan speed.");
      Serial.println("stop - Stop fan.");
      Serial.println("reverse - Reverse fan.");
      Serial.println("sendbinary - Send a custom 24-bit binary sequence.");
    } 
    else 
    {
      Serial.println("Invalid command!");
    }
  }
}

void troll()
{
  for (int i = 0; i <= 20; i++){
    mySwitch.send(assembleCommand(1, 0));
    delay(1000);
    mySwitch.send(assembleCommand(2, 0));
    delay(1000);
    mySwitch.send(assembleCommand(3, 0));
    delay(1000);
  }
}

void transmitCommand(char* command){
  matrix.renderBitmap(transmit, 8, 12);
  mySwitch.send(command);
  matrix.renderBitmap(off, 8, 12);
}

char* assembleCommand(int fan, int command) {
  char* identifier = NULL;
  char* commandByte = NULL;
  static char radioCommand[25]; // 24 bits + null terminator

  switch (fan) {
    case 1: // Secondary Bedroom
      identifier = "0111110111100011";
      break;
    case 2: // Living Room
      identifier = "1100100111101111";
      break;
    case 3: // Master Bedroom
      identifier = "0100101111101101";
      break;
    default:
      Serial.println("ERROR: Attempted to issue command on fan that does not exist!");
  }

  switch (command) {
    case 0: // Light
    {
      commandByte = "10100000";
      break;
    }
    case 1: // Speed 1
    {
      commandByte = "11000000";
      break;
    }
    case 2: // Speed 2
    {
      commandByte = "10011000";
      break;
    }
    case 3: // Speed 3
    {
      commandByte = "01000000";
      break;
    }
    case 4: // Speed 4
    {
      commandByte = "01010000";
      break;
    }
    case 5: // Speed 5
    {
      commandByte = "10010100";
      break;
    }
    case 6: // Speed 6
    {
      commandByte = "10000000";
      break;
    }
    case 7: // Stop
    {
      commandByte = "00100000";
      break;
    }
    case 8: // Reverse
    {
      commandByte = "11010000";
      break;
    }
    default:
    {
      Serial.println("ERROR: Attempted to issue a command that doesn't exist!");
      break;
    }
  }
  
  strcpy(radioCommand, identifier);  // Copy the identifier to radioCommand
  strcat(radioCommand, commandByte);  // Concatenate the light command
  Serial.println(radioCommand);

  return radioCommand;
}

