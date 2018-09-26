Implement Texas flash programming protocol via Arduino board.
See
http://www.ti.com/tool/cc-debugger
and
http://akb77.com/g/rf/program-cc-debugger-cc2511-with-arduino/
for details

****Experimental branch.****

For it to work with 2000000 baud rate.
Change SERIAL_RX_BUFFER_SIZE from 64 to 256
In HardwareSerial.h (found inside the arduino core)

