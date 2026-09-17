#include "pulse_protocol.h"
#include <string.h>
uint16_t Pulse_Crc(const uint8_t *bytes, uint8_t size)
{
  uint16_t crc = 0xffffU;
  for (uint8_t i=0; i<size; ++i)
  {
    crc ^= (uint16_t)bytes[i] << 8;
    for (uint8_t bit=0; bit<8; ++bit)
      crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U));
  }
  return crc;
}
static void Drop(PulseParser *p, uint8_t n)
{
  p->used -= n;
  memmove(p->bytes, p->bytes+n, p->used);
}
void PulseParser_Byte(PulseParser *p, uint8_t byte, uint32_t now,
                      PulseCommand command, PulseSend send)
{
  if ((uint32_t)(now-p->last_byte) > 1000U) p->used=0;
  p->last_byte=now;
  p->bytes[p->used++]=byte;
  while (p->used)
  {
    uint8_t *b=p->bytes;
    if (b[0]!=0x50 || (p->used>1 && b[1]!=0x4c)) { Drop(p,1);continue; }
    if (p->used<7) return;
    if (b[2]!=1 || b[6]>32) { Drop(p,1);continue; }
    uint8_t total=9+b[6];
    if (p->used<total) return;
    uint16_t crc=(uint16_t)b[total-2] | ((uint16_t)b[total-1]<<8);
    if (Pulse_Crc(b+2,5+b[6])!=crc) { Drop(p,1);continue; }
    uint8_t type=b[3], size=b[6], status=0;
    const uint8_t *data=b+7;
    if (type==1 || type==3) status=size ? 1 : 0;
    else if (type==2)
    {
      if (size!=5 || data[0]>2 || (data[1]+((uint16_t)data[2]<<8))>10000
          || data[3]>100 || data[4]<50 || data[4]>200) status=1;
    }
    else status=3;
    /* Ignore response frames to avoid echo loops on a shared debug UART. */
    if (type<0x80)
    {
      if (!status) status=command(type,data);
      uint8_t reply[10]={0x50,0x4c,1,(uint8_t)(type|0x80),b[4],b[5],1,status,0,0};
      crc=Pulse_Crc(reply+2,6);reply[8]=(uint8_t)crc;reply[9]=(uint8_t)(crc>>8);
      send(reply,sizeof(reply));
    }
    Drop(p,total);
  }
}
