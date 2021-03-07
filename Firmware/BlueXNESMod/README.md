# BlueXNESMod
This project can be used with NES or SNES controller.  

You have to define the applicable controller type at the beginning of the code.  
____  
    #define NES  
or  
____  
    #define SNES  
Afterwards compile and flash the project as normal.  

## Wiring:

**WARNING: If you connect ESP32 to 5V driven controller, you have to add a level converter circuit.**  

- Connect 3,3V/5V to NES/SNES controller's (*depends if your controller is 3,3V compatible*)  

- Connect pin 13 to NES/SNES controller's latch pin
  
- Connect pin 14 to NES/SNES controller's clock pin  

- Connect pin 15 to NES/SNES controller's data pin  

- Connect GND to controller's ground pin  


**NES Connector**
____  
             ____
            /    |  
           /   o | -> GND  
    VCC<- | o  o | -> Clock  
    NC<-  | o  o | -> Latch  
    NC<-  | o  o | -> Data  
          |______|

**SNES Connector**
____
      _  
     / \  
     |o| -> GND  
     |o| -> NC  
     |o| -> NC  
     |-|  
     |o| -> Data  
     |o| -> Latch  
     |o| -> Clock  
     |o| -> VCC  
     |_|  


## Build instructions(v2):

- Use this esp-idf fork here: https://github.com/NathanReeves/esp-idf  

- Set up the esp-idf environment: https://docs.espressif.com/projects/esp-idf/en/v3.1.7/get-started/index.html  

- Get the BlueCubeModv2 firmware  

- If you haven’t flashed an ESP32 project before, you need the port name of ESP32 for the config file. If using unix system, to get the port name of a USB device run:

`ls /dev`

- Find your device on the list and copy it. It should look something like: /dev/cu.usbserial-DO01EXOV or /dev/cu.SLAB_USBtoUART

- cd into project folder and run:

`make menuconfig`

- Paste your port name into Serial Flasher Config -> Default Serial Port

- Compile and flash the program, run:

`make flash monitor`


Resources used:

https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering

https://github.com/timmeh87/switchnotes

https://github.com/turicas/SNES


Thank you to [@Molorius]( https://github.com/Molorius ) for implementing the bluedroid Classic stack for esp

Thank you to [@NathanReeves]( https://github.com/NathanReeves ) for developing the main code for the switch Pro Controller connection.

