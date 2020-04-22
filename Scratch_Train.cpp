/*
  Scratch_Train.cpp

  Created by Peter Wallen on 29/03/2020
  Version 1.0
 
Copyright © 2020 Peter Wallen.
 
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 
 This C++ code demonstrates how a micro:bit can be interfaced to a Scratch program using the
 micro:bit extension and Scratch Link to send commands to the DCC++ Arduino software to control
 a model railway.
 
 The code must be compiled using micro:bit C++ development tools, e.g. ARM mbed online IDE for C/C++
 available at http://developer.mbed.org/, to produce a hex file suitable for downloading onto a
 micro:bit.
 
 The code is packaged here as a self-conatined source file (e.g. no separate header file) for
 convenience rather good practice and style.
 
 Acknowledgement:
 
 My thanks to Koji Yokokawa for his code on Github - pxt-scratch-more which showed me how I could
 code a suitable BLE service that works with the Scratch Link communication interface.
 
Please note: unlike the scratch-more service, this BLE interface is only designed to work with
certain blocks provide with the standard micro:bit extension e.g. blocks that support the
 micro:bit accelerometer are not supported.
 
The micro:bit runtime must be configured to support BLE so that it interfaces with Scratch Link.
This should be done using config.json if using the offline development environment or directly to
MicroBitConfig.h if using the online editor - e.g:
 
 #define MICROBIT_BLE_ENABLED                    1
 #define MICROBIT_BLE_PAIRING_MODE               1
 #define MICROBIT_BLE_PRIVATE_ADDRESSES          0
 #define MICROBIT_BLE_OPEN                       1
 #define MICROBIT_BLE_SECURITY_LEVEL             SECURITY_MODE_ENCRYPTION_NO_MITM
 #define MICROBIT_BLE_WHITELIST                  0
 #define MICROBIT_BLE_ADVERTISING_TIMEOUT        0
 #define MICROBIT_BLE_DEFAULT_TX_POWER           6
 
*/

#include "MicroBit.h"

#define SCRATCH_ID 2000
#define SCRATCH_EVT_NOTIFY 1
#define SCRATCH_TRAIN_ID 2001
#define SCRATCH_TRAIN_EVT_INPUT 1

#define ON  1
#define OFF 0

/*
 
 functionTable - helps generate appropriate CAB functions for the DCC++ Base Station commands.
 The table is used to convert a 5 bit integer (0 .. 31) into a suitable <f> command.
 Each of the 32 entries specifies 4 integers:
     0 - function group base value: group 0 = 128, group 1 = 176, group 2 = 160
     1 - function value with group: either 0,1,2,4,8,16
     2 - function group: 0 - CAB functions F0 - F4, 1 - CAB functions F5 - F8, 2 - CAB functions F9 - F12
     3 - function position used in the functionGroups array  (0 - 4).
 
 Note: The table only supports CAB functions F0 - F12.
 
 The first 5 entries in the table are reserved for non CAB function commands.
 
*/

const static int functionTable[32][4] = {
                                                {0,0,0,0},      // 0
                                                {0,0,0,0},      // 1
                                                {0,0,0,0},      // 2
                                                {0,0,0,0},      // 3
                                                {0,0,0,0},      // 4
                                                {128,16,0,4},   // 5  F0 on
                                                {128,0,0,4},  // 6  F0 off
                                                {128,1,0,0},    // 7  F1 on
                                                {128,0,0,0},   // 8  F1 off
                                                {128,2,0,1},    // 9  F2 on
                                                {128,0,0,1},   // 10 F2 off
                                                {128,4,0,2},    // 11 F3 on
                                                {128,0,0,2},   // 12 F3 off
                                                {128,8,0,3},    // 13 F4 on
                                                {128,0,0,3},   // 14 F4 off
                                                {176,1,1,0},    // 15 F5 on
                                                {176,0,1,0},   // 16 F5 off
                                                {176,2,1,1},    // 17 F6 on
                                                {176,0,1,1},   // 18 F6 off
                                                {176,4,1,2},    // 19 F7 on
                                                {176,0,1,2},   // 20 F7 off
                                                {176,8,1,3},    // 21 F8 on
                                                {176,0,1,3},   // 22 F8 off
                                                {160,1,2,0},    // 23 F9 on
                                                {160,0,2,0},   // 24 F9 off
                                                {160,2,2,1},    // 25 F10 on
                                                {160,0,2,1},   // 26 F10 off
                                                {160,4,2,2},    // 27 F11 on
                                                {160,0,2,2},   // 28 F11 off
                                                {160,8,2,3},    // 29 F12 on
                                                {160,0,2,3},   // 30 F12 off
                                                {0,0,0,0},      // 31 reserved.
                                            };
/*
 The ScratchService class represents the communication link between Scratch Link and the micro:bit.
 It advertises the BLE service F005 used by Scratch Link.
 The service has 2 charcteristics:
     txCharcteristic - transmit messages to the users scratch program.
     rxCharcteristic - receive messages from the users Scratch program.
*/
class ScratchService {
    public:
        MicroBit &uBit;
        
        uint8_t txData[20];
        uint8_t rxBuffer[20];
        
        GattAttribute::Handle_t txCharacteristicHandle;
        GattAttribute::Handle_t rxCharacteristicHandle;
        
        /*
        sensors - an array of 6 flags represnting sensor responses <Qn> from DCC++
              <Q1> - sensors[2]   - When Pin 0 Connected
              <Q2> - sensors[3]   - When Pin 1 Connected
              <Q3> - sensors[4]   - When Pin 2 Connected
              <Q4> - sensors[0]   - When A Button Pressed
              <Q5> - sensors[1]   - When B Button Pressed
        */
        
        uint8_t sensors[5];
        
        /*
        command - contains a valid DCC++ command sent by the Scratch program
        The command is either:
         - directly derived from 'display text' block
         - interpreted from 'display' block (see function build command)
        */
        
        ManagedString command;
        
        /*
        functionGroups is updated by the function build command and represents the state of CAB functions
        for each of the 3 groups.
        */
        
        int functionGroups[3][5];
        
        //ScratchService constructor
        
        ScratchService(MicroBit &_uBit) : uBit(_uBit) {
           
           const uint8_t ScratchServiceTxUUID[] = {
            0x52, 0x61, 0xda, 0x01, 0xfa, 0x7e, 0x42, 0xab, 0x85, 0x0b, 0x7c, 0x80, 0x22, 0x00, 0x97, 0xcc};
           
           GattCharacteristic txCharacteristic(
                                        ScratchServiceTxUUID,
                                        (uint8_t *)&txData,
                                        0,
                                        sizeof(txData),
                                        GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_READ | 
                                        GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_NOTIFY);
                                        
            const uint8_t ScratchServiceRxUUID[] = {
            0x52, 0x61, 0xda, 0x02, 0xfa, 0x7e, 0x42, 0xab, 0x85, 0x0b, 0x7c, 0x80, 0x22, 0x00, 0x97, 0xcc};
            
           GattCharacteristic rxCharacteristic(
                                        ScratchServiceRxUUID,
                                        (uint8_t *)&rxBuffer,
                                        0,
                                        sizeof(rxBuffer),
                                        GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE |
                                        GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE_WITHOUT_RESPONSE); 
                                        
            
            GattCharacteristic *characteristics[] = {&txCharacteristic, &rxCharacteristic};
            
            const uint16_t ScratchServiceUUID = 0xf005;
            
            GattService service(
                        ScratchServiceUUID, characteristics,
                        sizeof(characteristics) / sizeof(GattCharacteristic *));
                        
            uBit.ble->addService(service);
            
            txCharacteristicHandle = txCharacteristic.getValueHandle();
            rxCharacteristicHandle = rxCharacteristic.getValueHandle();
            
            // Write initial value.
            uBit.ble->gattServer().write(
                                 txCharacteristicHandle,
                                 (uint8_t *)&txData,
                                 sizeof(txData));
                              
            // Advertise this service.
            const uint16_t uuid16_list[] = {ScratchServiceUUID};
            ble_error_t rc = uBit.ble->accumulateAdvertisingPayload(GapAdvertisingData::INCOMPLETE_LIST_16BIT_SERVICE_IDS, (uint8_t *)uuid16_list, sizeof(uuid16_list));
        
            // Setup callbacks for events
            if (EventModel::defaultEventBus) {
             EventModel::defaultEventBus->listen(SCRATCH_ID, SCRATCH_EVT_NOTIFY, this, &ScratchService::notify, MESSAGE_BUS_LISTENER_IMMEDIATE);
            } 
            uBit.ble->onDataWritten(this, &ScratchService::onDataWritten);
            
            // Intialise the function groups array
            int groups = sizeof functionGroups / sizeof functionGroups[0];
            int functions = sizeof functionGroups[0] / sizeof(int);
            for (int x = 0; x < groups ;x++) {
                for (int y = 0; y < functions; y++) {
                    functionGroups[x][y] = 0;
                }
            }                          
        }
        
        /* 
            Set Sensor Flag
            
        */
        
        void setSensor(int sensor) {
            switch(sensor) {
                case 0:              // pin 0
                sensors[2] = 1;
                break;
                case 1:              // pin 1
                sensors[3] = 1;
                break;
                case 2:              // pin 2
                sensors[4] = 1;
                break;
                case 3:              // Button A
                sensors[0] = 1;
                break;
                case 4:              // Button B
                sensors[1] = 1;
                break;   
            }
        }
        /*
             Reads the DCC++ command string sent using the Scratch 'Display Text' block or derived
             from the Scratch 'Display' block.
        */
        ManagedString getCommand() {
            return command;
        }
        /*
            Build the appropriate DCC++ command form the binary image sent using the Scratch 'Display' block
        */
        ManagedString buildCommand(uint8_t *data) {
            char command[20];
            int  address = data[2];
            int speed = data[3] * 4;
            if (data[1] < 5) {
                switch (data[1]) {
                    case 0:
                    sprintf(command,"<t 1 %d 0 0>",address);
                    break;
                    case 1:
                    sprintf(command,"<t 1 %d %d 1>",address,speed);
                    break;
                    case 2:
                    sprintf(command,"<t 1 %d %d 0>",address,speed);
                    break;
                    case 3:
                    sprintf(command,"<a %d 0 0>",address);
                    break;
                    case 4:
                    sprintf(command,"<a %d 0 1>",address);
                    break;
                }
            } else {
                int functionCommand = data[1];
                int functionCode = functionTable[functionCommand][0];
                int groupValue = functionTable[functionCommand][1];
                int groupNumber = functionTable[functionCommand][2];
                int groupPosition = functionTable[functionCommand][3];
                functionGroups[groupNumber][groupPosition] = groupValue;
                int functionMax = sizeof functionGroups[0] / sizeof(int);
                for (int i = 0; i < functionMax; i++) {
                    functionCode = functionCode + functionGroups[groupNumber][i];
                }
                sprintf(command,"<f %d %d>",address,functionCode);
            }
            
            return command;
        }
        
        /*
            Read data from Scratch
        */
        void onDataWritten(const GattWriteCallbackParams *parms) {
             uint8_t *data = (uint8_t *)parms->data;
             if (parms->handle == rxCharacteristicHandle && parms->len > 0) {
                if (data[0] == 0x81) {
                    char text[parms->len];
                    memcpy(text, &(data[1]), (parms->len) - 1);
                    text[(parms->len) - 1] = '\0';
                    command = ManagedString(text);
                    MicroBitEvent ev(SCRATCH_TRAIN_ID, SCRATCH_TRAIN_EVT_INPUT);
                    uBit.display.scroll(command, 120);
                }    
                else if (data[0] == 0x82) {
                    uBit.display.stopAnimation();
                    command = buildCommand(data);
                    MicroBitEvent ev(SCRATCH_TRAIN_ID, SCRATCH_TRAIN_EVT_INPUT);
                    for (int y = 1; y < parms->len; y++) {
                        for (int x = 0; x < 5; x++) {
                            uBit.display.image.setPixelValue(x, y - 1, (data[y] & (0x01 << x)) ? 255 : 0);
                        }
                    }
                }
             }
        }
        void setBuffer(uint8_t *buff) {
            int bufferSize =  sizeof(txData) / sizeof(txData[0]);
            for (int i = 0; i < bufferSize; i++ ) {
                buff[i] = 0;
            }
            int sensorsMax = sizeof(sensors) / sizeof(sensors[0]);
            for (int i = 0; i < sensorsMax; i++ ) {
                buff[i + 4] = sensors[i];
            }
            
        }
        void clearSensors() {
            int sensorsMax = sizeof(sensors) / sizeof(sensors[0]);
            for (int i = 0; i < sensorsMax; i++) {
                sensors[i] = 0;
            }
        }
        
        /*
          Send data to Scratch
        */
        void notify(MicroBitEvent) {
            
            if (uBit.ble->getGapState().connected) {
                setBuffer(txData);
                uBit.ble->gattServer().notify(
                                        txCharacteristicHandle,
                                        (uint8_t *)&txData,
                                        sizeof(txData) / sizeof(txData[0]));
            } 
            clearSensors();    
        }
};

MicroBit uBit;

ManagedString dcc_eom(">");

ScratchService* scratchService = NULL;

int lock = OFF;

// Event Handlers

void onConnected(MicroBitEvent) {
    uBit.display.print("C");
}
void onDisconnected(MicroBitEvent) {
    uBit.display.print("D");
}
void onButton(MicroBitEvent e) {
    if(e.source == MICROBIT_ID_BUTTON_A) {
        scratchService->setSensor(3);
    }
    if(e.source == MICROBIT_ID_BUTTON_B) {
        scratchService->setSensor(4);
    }
}
void onScratchMessage(MicroBitEvent) {
     if (lock == OFF) {
        lock = ON;
        uBit.serial.send(scratchService->getCommand() );
        lock = OFF;
    }
}

// Keep Scratch Link alive poll every 200 milliseconds
void notifyScratch() {
    while (1) {
        MicroBitEvent ev(SCRATCH_ID, SCRATCH_EVT_NOTIFY);
        fiber_sleep(200);
    }
}

ManagedString clean(ManagedString message) {
    ManagedString newString;
    for (int i = 0; i < message.length(); i++) {
        char c = message.charAt(i);
        if (c < 127) {
            newString = newString + ManagedString(c);
        }
    }
    return newString;
}
void sendToScratch(ManagedString dccResponse) {
    ManagedString command = clean(dccResponse);
    ManagedString commandPrefix = command.substring(0,2);
    if (commandPrefix == "<Q") {
        char sensor = command.charAt(2);
        switch (sensor) {
            case '1' :
            scratchService->setSensor(0);
            break;
            case '2' :
            scratchService->setSensor(1);
            break;
            case '3' :
            scratchService->setSensor(2);
            break;
            case '4' :
            scratchService->setSensor(3);
            break;
            case '5' :
            scratchService->setSensor(4);
            break;
        }
    }
}

int main()
{
    // Initialise the micro:bit runtime.
    uBit.init();
    /*
    Use the microbit serial interface to communicate with the DCC++ program running on the Arduino
    Transmit data on Pin 0  
    Receive data on Pin 1
    Use default baud rate of 115200
    */
    uBit.serial.redirect(MICROBIT_PIN_P0,MICROBIT_PIN_P1);
    /*
    Instantiate a scratchService object to communicate with Scratch using BLE.
    Schedule a fibre to run in the background that frequently requests the scratchObject to send a message to scratch.
    */ 
    scratchService = new ScratchService(uBit);
    create_fiber(notifyScratch);
    // Setup event handlers 
    uBit.messageBus.listen(MICROBIT_ID_BLE, MICROBIT_BLE_EVT_CONNECTED, onConnected);
    uBit.messageBus.listen(MICROBIT_ID_BLE, MICROBIT_BLE_EVT_DISCONNECTED, onDisconnected);
    uBit.messageBus.listen(SCRATCH_TRAIN_ID, SCRATCH_TRAIN_EVT_INPUT, onScratchMessage);
    uBit.messageBus.listen(MICROBIT_ID_BUTTON_A,MICROBIT_EVT_ANY,onButton,MESSAGE_BUS_LISTENER_IMMEDIATE);
    uBit.messageBus.listen(MICROBIT_ID_BUTTON_B,MICROBIT_EVT_ANY,onButton,MESSAGE_BUS_LISTENER_IMMEDIATE);
    // Displaty intial message on the microbit 
    uBit.display.print("S");
    // Poll the DCC++ program on the Arduino for messages e.g. when a senor has been detected.
    while(1) {
        ManagedString dccResponse = uBit.serial.readUntil(dcc_eom);
        sendToScratch(dccResponse);
    }
}

