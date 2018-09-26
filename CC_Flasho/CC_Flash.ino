#include <FastGPIO.h>

#include <ctype.h>

#define LED               LED_BUILTIN
#define CC_DC             5
#define CC_DD             6
#define CC_RST            7

#define BUFFER_SIZE       140

#define READ_TYPE_CODE    0
#define READ_TYPE_XDATA   1

byte inBuffer[BUFFER_SIZE];
byte inDataLen = 0;
byte idx;

void setup() {
  CLKPR = 0x80;
  CLKPR = 0;

  FastGPIO::Pin<LED>::setOutputLow();

  FastGPIO::Pin<CC_DC>::setOutputLow();
  FastGPIO::Pin<CC_DD>::setOutputLow();
  FastGPIO::Pin<CC_RST>::setInputPulledUp();

  Serial.begin(115200);
  LED_OFF();
  Serial.print("\r\n:");
}

void loop() {
  if (Serial.available() <= 0)
    return;

  byte inByte = Serial.read();
  if (inByte == '\r') {
    process_cmd();
    inDataLen=0;
  } else if (inDataLen >= BUFFER_SIZE) {
    do {
      inByte = Serial.read();
    } while (inByte != '\r');
    Serial.println();
    Serial.print("BUFFER OVERFLOW");
    sendERROR();
    inDataLen=0;
  } else {
    inByte = toupper(inByte);
    if (inByte < ' ') {
      do {
        inByte = Serial.read();
      } while (inByte != '\r');
      Serial.println();
      Serial.print("BAD BYTE RECEIVED:");
      Serial.print(inByte);
      sendERROR();
      inDataLen=0;
    }
    inBuffer[inDataLen++] = inByte;
  }  
}

void process_cmd() {
  Serial.println();
  
  if (!inDataLen) {
    sendOK();
    return;
  }
  
  byte csum = checkChecksum();
  if (csum) {
    Serial.print("BAD CHECKSUM:");
    printHexln(0-csum);
    sendERROR();
    return;
  }
  
  // Remove checksum from length
  inDataLen -= 2;

  switch(inBuffer[0]) {
    case 'D': CMD_ENTER_DEBUG();    break;
    case 'L': CMD_LED();            break;
    case 'R': CMD_RESET();          break;
    case 'M': CMD_XDATA();          break;
    case 'X': CMD_EXTENDED();       break;
    case 'B': CMD_BAUD();           break;
    case 'T': CMD_TEST();           break;
    default:
      Serial.print("BAD COMMAND:");
      Serial.println(inBuffer[0]);
      sendERROR();
  }  
}

void CMD_TEST() {
  if (inDataLen != 3) {
    sendERROR();
    return;
  }
  if (isHexByte(1))
    if (getHexByte(1) == 0x56) {
      sendOK();
      return;
    }
}
void CMD_BAUD() {
  if (inDataLen != 7) {
    sendERROR();
    return;
  }
  if (isHexByte(1) && isHexByte(3) && isHexByte(5)) {
    uint32_t b1 = getHexByte(1);
    uint32_t b2 = getHexByte(3);
    uint32_t b3 = getHexByte(5);
    uint32_t baud = (b1 << 16) | (b2 << 8) | b3;
    
    Serial.print("BAUD:");
    printHex((baud >> 16) & 0xFF);
    printHex((baud >> 8) & 0xFF);
    printHexln(baud & 0xFF);
    sendOK();
    
    Serial.end();
    Serial.begin(baud);
    return;
  } else {
    sendERROR();
    return;
  }
}

void CMD_ENTER_DEBUG() {
  if (inDataLen != 1) {
    sendERROR();
    return;
  }

  dbg_enter();
  dbg_instr(0x00);
  sendOK();
}

void CMD_LED() {
  if (inDataLen != 2) {
    sendERROR();
    return;
  }

  switch (inBuffer[1]) {
    case '0': LED_OFF();                                    break;
    case '1': LED_ON();                                     break;
    case '3': FastGPIO::Pin<CC_DC>::setOutputValueHigh();   break;
    case '4': FastGPIO::Pin<CC_DC>::setOutputValueLow();    break;
    case '5': FastGPIO::Pin<CC_DD>::setOutputValueHigh();   break;
    case '6': FastGPIO::Pin<CC_DD>::setOutputValueLow();    break;
    case '7': FastGPIO::Pin<CC_RST>::setOutputLow();        break;
    case '8': FastGPIO::Pin<CC_RST>::setInputPulledUp();    break;
  }
  sendOK();
}

void CMD_RESET() {
  if (inDataLen != 2) {
    sendERROR();
    return;
  }
  
  switch (inBuffer[1]) {
    case '0': dbg_reset(0);   break;
    case '1': dbg_reset(1);   break;
  }
  sendOK();
}

void CMD_XDATA() {
  if (inDataLen < 8 || !isHexByte(2) || !isHexByte(4) || !isHexByte(6)) {
    sendERROR();
    return;
  }
  
  byte cnt = getHexByte(6);
  if (!cnt) {
    sendOK();
    return;
  }
  if (cnt > 64) {
    Serial.println("NO MORE THAN 64 BYTES");
    sendERROR();
    return;
  }
  
  LED_ON();
  switch (inBuffer[1]) {
    case 'W': CMD_XDATA_WRITE(cnt);                   break;
    case 'R': CMD_XDATA_READ(cnt, READ_TYPE_XDATA);   break;
    case 'C': CMD_XDATA_READ(cnt, READ_TYPE_CODE);    break;
    default:
      sendERROR();
  }
  LED_OFF();
}

void CMD_XDATA_WRITE(byte cnt) {
  idx = 8;
  dbg_instr(0x90, getHexByte(2), getHexByte(4));      // MOV DPTR #high #low
  while (cnt-- > 0) {
    if (idx+1 >= inDataLen) {
      Serial.println("NO DATA");
      sendERROR();
      return;
    }
    if (!isHexByte(idx)) {
      Serial.println("NO HEX");
      sendERROR();
      return;
    }
    
    dbg_instr(0x74, getHexByte(idx));
    dbg_instr(0xF0);
    dbg_instr(0xA3);
    idx += 2;
  }
  sendOK();
}

void CMD_XDATA_READ(byte cnt, byte type) {
  byte data, csum = 0;
  dbg_instr(0x90, getHexByte(2), getHexByte(4));      // MOV DPTR #high #low
  Serial.print("READ:");
  while (cnt-- > 0) {
    if (type == READ_TYPE_XDATA)
      data = dbg_instr(0xE0);
    else {
      dbg_instr(0xE4);
      data = dbg_instr(0x93);
    }
    dbg_instr(0xA3);
    csum += data;
    printHex(data);
  }
  csum = (0 - (~csum));
  printHexln(csum);
  sendOK();
}

void CMD_EXTENDED() {
  LED_ON();
  idx = 1;
  bool ok = true;
  while (ok && idx < inDataLen) {    
    switch (inBuffer[idx++]) {
      case 'W': ok = CMD_EXTENDED_WRITE();  break;
      case 'R': ok = CMD_EXTENDED_READ();   break;
      default:
        ok = false;
    }
  }
  LED_OFF();
  if (ok) {
    sendOK();    
  } else {
    sendERROR();
  }
}

bool CMD_EXTENDED_WRITE() {
  if (idx >= inDataLen || !isdigit(inBuffer[idx]))
    return false;
    
  byte cnt = inBuffer[idx++] - '0';
  if (!cnt)
    return true;
  while (cnt-- > 0) {
    if (idx+1 >= inDataLen || !isHexByte(idx))
      return false;
    dbg_write(getHexByte(idx));
    idx += 2;
  }
  cc_delay(10);
  return true;
}

bool CMD_EXTENDED_READ() {
  if (idx >= inDataLen || !isdigit(inBuffer[idx]))
    return false;

  Serial.print("READ:");
  byte cnt = inBuffer[idx++] - '0';
  byte csum = 0;
  while (cnt-- > 0) {
    byte data = dbg_read();
    csum += data;
    printHex(data);
  }
  csum = (0 - (~csum));
  printHexln(csum);
  return true;
}

byte isHexDigit(unsigned char c) {
  if (c >= '0' && c <= '9')
    return 1;
  else if (c >= 'A' && c <= 'F')
    return 1;
  return 0;
}

byte isHexByte(byte index) {
  return isHexDigit(inBuffer[index]) & isHexDigit(inBuffer[index + 1]);
}

byte getHexDigit(unsigned char c) {
  if (c >= '0' && c <= '9')
    c -= '0';
  else if (c >= 'A' && c <= 'F')
    c -= ('A' - 10);
  return c;
}

byte getHexByte(byte index) {
  return ((getHexDigit(inBuffer[index]) << 4) | getHexDigit(inBuffer[index + 1]));
}

byte checkChecksum() {
  if (inDataLen <= 2)
    return 0;

  byte csum = 0;
  byte imax = inDataLen - 2;
  byte i = 0;
  for(; i < imax; ++i)
    csum += inBuffer[i];
  csum = ~csum;
  return (csum + getHexByte(i));
}

void LED_OFF() {
  FastGPIO::Pin<LED>::setOutputValueLow();
}

void LED_ON() {
  FastGPIO::Pin<LED>::setOutputValueHigh();
}

void LED_TOGGLE() {
  FastGPIO::Pin<LED>::setOutputValueToggle();
}

void BlinkLED(byte blinks) {
  while (blinks-- > 0) {
    LED_ON();
    delay(500);
    LED_OFF();
    delay(500);
  }
  delay(1000);
}

void cc_delay( unsigned char d ) {
  volatile unsigned char i = d;
  while( i-- );
}

inline void dbg_clock_high() {
  FastGPIO::Pin<CC_DC>::setOutputValueHigh();
}

inline void dbg_clock_low() {
  FastGPIO::Pin<CC_DC>::setOutputValueLow();
}

// 1 - activate RESET (low)
// 0 - deactivate RESET (high)
void dbg_reset(unsigned char state) {
  if (state) {
    FastGPIO::Pin<CC_RST>::setOutputLow();
  } else {
    FastGPIO::Pin<CC_RST>::setInputPulledUp();
  }
  cc_delay(200);
}

void dbg_enter() {
  dbg_reset(1);
  dbg_clock_high();
  cc_delay(1);
  dbg_clock_low();
  cc_delay(1);
  dbg_clock_high();
  cc_delay(1);
  dbg_clock_low();
  cc_delay(200);
  dbg_reset(0);
}

byte dbg_read() {
  byte cnt, data;
  FastGPIO::Pin<CC_DD>::setInput();
  for (cnt = 8; cnt; cnt--) {
    dbg_clock_high();
    data <<= 1;
    asm("nop \n");
    if (FastGPIO::Pin<CC_DD>::isInputHigh())
      data |= 0x01;
    dbg_clock_low();
  }
  FastGPIO::Pin<CC_DD>::setOutputLow();
  return data;
}

void dbg_write(byte data) {
  byte cnt;
  FastGPIO::Pin<CC_DD>::setOutputLow();
  for (cnt = 8; cnt; cnt--) {
    if (data & 0x80)
      FastGPIO::Pin<CC_DD>::setOutputValueHigh();
    else
      FastGPIO::Pin<CC_DD>::setOutputValueLow();
    dbg_clock_high();
    data <<= 1;
    dbg_clock_low();
  }
  FastGPIO::Pin<CC_DD>::setOutputValueLow();
}

void printHex(unsigned char data) {
  if (data < 0x10)
    Serial.print('0');
  Serial.print(data, HEX);
}

void printHexln(unsigned char data) {
  if (data < 0x10)
    Serial.print('0');
  Serial.println(data, HEX);
}

byte dbg_instr(byte in0, byte in1, byte in2) {
  dbg_write(0x57);
  dbg_write(in0);
  dbg_write(in1);
  dbg_write(in2);
  //cc_delay(6);
  return dbg_read();
}

byte dbg_instr(byte in0, byte in1) {
  dbg_write(0x56);
  dbg_write(in0);
  dbg_write(in1);
  //cc_delay(6);
  return dbg_read();
}

byte dbg_instr(byte in0) {
  dbg_write(0x55);
  dbg_write(in0);
  //cc_delay(6);
  return dbg_read();
}

void sendERROR() {
  Serial.print("ERROR\r\n:");
}

void sendOK() {
  Serial.print("OK\r\n:");
}
