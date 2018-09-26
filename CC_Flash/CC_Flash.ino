#include <FastGPIO.h>

#include <ctype.h>

#define LED               LED_BUILTIN
#define CC_DC             13
#define CC_DD             12
#define CC_RST            7

#define PACKET_SIZE       (64 * 3)
#define BUFFER_SIZE       PACKET_SIZE + 9

#define READ_TYPE_CODE    0
#define READ_TYPE_XDATA   1

byte inBuffer[BUFFER_SIZE];
byte inDataLen = 0, totalLen = 0;
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
}

void loop() {
  if (Serial.available() <= 0)
    return;

  byte inByte = Serial.read();

  if (totalLen == 0)
    totalLen = inByte;
  else
    inBuffer[inDataLen++] = inByte;
  //Serial.write(inByte);
  if (inDataLen == totalLen) {
    process_cmd();
    inDataLen=0;
    totalLen=0;
  } else if (totalLen > BUFFER_SIZE) {
    do {
      inByte = Serial.read();
    } while (inDataLen < totalLen);
    Serial.write(0x03);
    Serial.write('E');
    Serial.print("BO");
    inDataLen=0;
    totalLen=0;
  }
}

void process_cmd() {
  if (!inDataLen) {
    Serial.write(0x01);
    Serial.write('O');
    return;
  }
  
  byte csum = checkChecksum();
  if (csum) {
    Serial.write(0x04);
    Serial.write('E');
    Serial.print("BS");
    Serial.write(0 - csum);
    return;
  }
  
  // Remove checksum from length
  inDataLen -= 1;

  switch(inBuffer[0]) {
    case 'D': CMD_ENTER_DEBUG();    break;
    case 'L': CMD_LED();            break;
    case 'R': CMD_RESET();          break;
    case 'M': CMD_XDATA();          break;
    case 'X': CMD_EXTENDED();       break;
    case 'B': CMD_BAUD();           break;
    case 'T': CMD_TEST();           break;
    default:
      Serial.write(0x04);
      Serial.write('E');
      Serial.print("BC");
      Serial.write(inBuffer[0]);
  }  
}

void CMD_TEST() {
  if (inDataLen != 2) {
    Serial.write(0x01);
    Serial.write('E');
    return;
  }
  if (inBuffer[1] == 0x56) {
    Serial.write(0x04);
    Serial.write('O');
    Serial.write("RD");
    Serial.write(PACKET_SIZE);
    return;
  }
}
void CMD_BAUD() {
  if (inDataLen != 4) {
    Serial.write(0x01);
    Serial.write('E');
    return;
  }
  
  uint32_t b1 = inBuffer[1];
  uint32_t b2 = inBuffer[2];
  uint32_t b3 = inBuffer[3];
  uint32_t baud = (b1 << 16) | (b2 << 8) | b3;

  Serial.write(0x06);
  Serial.write('O');
  Serial.print("BR");
  Serial.write((baud >> 16) & 0xFF);
  Serial.write((baud >> 8) & 0xFF);
  Serial.write(baud & 0xFF);
  
  Serial.end();
  Serial.begin(baud);
  return;
}

void CMD_ENTER_DEBUG() {
  if (inDataLen != 1) {
    Serial.write(0x01);
    Serial.write('E');
    return;
  }

  dbg_enter();
  dbg_instr(0x00);
  Serial.write(0x01);
  Serial.write('O');
}

void CMD_LED() {
  if (inDataLen != 2) {
    Serial.write(0x01);
    Serial.write('E');
    return;
  }

  switch (inBuffer[1]) {
    case 0: LED_OFF();                                    break;
    case 1: LED_ON();                                     break;
    case 3: FastGPIO::Pin<CC_DC>::setOutputValueHigh();   break;
    case 4: FastGPIO::Pin<CC_DC>::setOutputValueLow();    break;
    case 5: FastGPIO::Pin<CC_DD>::setOutputValueHigh();   break;
    case 6: FastGPIO::Pin<CC_DD>::setOutputValueLow();    break;
    case 7: FastGPIO::Pin<CC_RST>::setOutputLow();        break;
    case 8: FastGPIO::Pin<CC_RST>::setInputPulledUp();    break;
  }
  Serial.write(0x01);
  Serial.write('O');
}

void CMD_RESET() {
  if (inDataLen != 2) {
    Serial.write(0x01);
    Serial.write('E');
    return;
  }
  
  switch (inBuffer[1]) {
    case 0: dbg_reset(0);   break;
    case 1: dbg_reset(1);   break;
  }
  Serial.write(0x01);
  Serial.write('O');
}

void CMD_XDATA() {
  byte cnt = inBuffer[4];
  if (!cnt) {
    Serial.write(0x01);
    Serial.write('O');
    return;
  }
  if (cnt > PACKET_SIZE) {
    Serial.write(0x03);
    Serial.write('E');
    Serial.print("OF");
    return;
  }
  
  LED_ON();
  switch (inBuffer[1]) {
    case 'W': CMD_XDATA_WRITE(cnt);                   break;
    case 'R': CMD_XDATA_READ(cnt, READ_TYPE_XDATA);   break;
    case 'C': CMD_XDATA_READ(cnt, READ_TYPE_CODE);    break;
    default:
      Serial.write(0x01);
      Serial.write('E');
  }
  LED_OFF();
}

void CMD_XDATA_WRITE(byte cnt) {
  idx = 5;
  if ((idx + cnt) > inDataLen) {
    Serial.write(0x03);
    Serial.write('E');
    Serial.print("ND");
    return;
  }
  dbg_instr(0x90, inBuffer[2], inBuffer[3]);      //MOV DPTR #high #low
  while (cnt-- > 0) {
    dbg_instr(0x74, inBuffer[idx]);               //MOV A, #data
    dbg_instr(0xF0);                              //MOVX @DPTR, A
    dbg_instr(0xA3);                              //INC DPTR
    idx++;
  }
  Serial.write(0x01);
  Serial.write('O');
}

void CMD_XDATA_READ(byte cnt, byte type) {
  byte data, csum = 0;
  dbg_instr(0x90, inBuffer[2], inBuffer[3]);      //MOV DPTR #high #low
  Serial.write(0x03 + cnt + 1);
  Serial.write('O');
  Serial.print("RD");
  while (cnt-- > 0) {
    if (type == READ_TYPE_XDATA)
      data = dbg_instr(0xE0);                     //MOVX A, @DPTR
    else {
      dbg_instr(0xE4);                            //CLR A
      data = dbg_instr(0x93);                     //MOVC A, @A+DPTR
    }
    dbg_instr(0xA3);                              //INC DPTR
    csum += data;
    Serial.write(data);
  }
  csum = (0 - (~csum));
  Serial.write(csum);
}

void CMD_EXTENDED() {
  bool doRead = false;
  LED_ON();
  idx = 1;
  Serial.write(0xFF);
  bool ok = true;
  while (ok && idx < inDataLen) {    
    switch (inBuffer[idx++]) {
      case 'W': ok = CMD_EXTENDED_WRITE();  break;
      case 'R': ok = CMD_EXTENDED_READ(); doRead = true;  break;
      default:
        ok = false;
    }
  }
  LED_OFF();
  if (ok) {
    if (!doRead) {
      Serial.write(0x01);
      Serial.write('O');
    }
  } else {
    Serial.write(0x01);
    Serial.write('E');
  }
}

bool CMD_EXTENDED_WRITE() {
  if (idx >= inDataLen)
    return false;
    
  byte cnt = inBuffer[idx++];
  if (!cnt)
    return true;
  while (cnt-- > 0) {
    if (idx >= inDataLen)
      return false;
    dbg_write(inBuffer[idx++]);
  }
  return true;
}

bool CMD_EXTENDED_READ() {
  if (idx >= inDataLen)
    return false;

  byte cnt = inBuffer[idx++];
  byte csum = 0;

  Serial.write(0x03 + cnt + 1);
  Serial.write('O');
  Serial.print("RD");
  while (cnt-- > 0) {
    byte data = dbg_read();
    csum += data;
    Serial.write(data);
  }
  csum = (0 - (~csum));
  Serial.write(csum);
  return true;
}

byte checkChecksum() {
  if (inDataLen < 2)
    return 0;

  byte csum = 0;
  byte imax = inDataLen - 1;
  byte i = 0;
  for(; i < imax; ++i)
    csum += inBuffer[i];
  csum = ~csum;
  return (csum + inBuffer[i]);
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
