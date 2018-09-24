Implement Texas flash programming protocol via Arduino board.
See
http://www.ti.com/tool/cc-debugger
and
http://akb77.com/g/rf/program-cc-debugger-cc2511-with-arduino/
for details

1. Upload CC_Flash.ino or CC_Flash_SPI.ino(experimental) sketch to Arduino
2. Connect
    Target          Arduino
        DC (P2.1) - pin 5
        DD (P2.2) - pin 6
        RST       - pin 7
        GND       - GND
   
   Connect (for CC_Flash_SPI.ino variant) 
    Target
        DC (P2.1) - pin 13 (SCK)
        DD (P2.2) - pin 12,11 (MOSI, MISO)
        RST       - pin 7
        GND       - GND
3. Start program CC.Flash.exe (need .NET 2.0)
