#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "pulse_protocol.h"
#include "pulse_effect.h"
static unsigned sets, stops, replies;
static uint8_t response[10];
static PulseEffect effect;
static uint32_t now;
static uint8_t command(uint8_t type, const uint8_t *p)
{
  if (type == 2) { ++sets; PulseEffect_Set(&effect, now, p[1] | ((uint16_t)p[2]<<8), p[3], p[4]); }
  if (type == 3) { ++stops; effect.active = false; }
  return 0;
}
static void send(const uint8_t *p, uint8_t n) { assert(n==10);memcpy(response,p,n);++replies; }
static void feed(PulseParser *p, const uint8_t *b, unsigned n)
{for(unsigned i=0;i<n;++i) PulseParser_Byte(p,b[i],now,command,send);}
static void packet(PulseParser *p,uint8_t type,const uint8_t *data,uint8_t size)
{
  uint8_t b[41]={0x50,0x4c,1,type,0xff,0xff,size};
  if(size) memcpy(b+7,data,size);
  uint16_t crc=Pulse_Crc(b+2,5+size);b[7+size]=crc;b[8+size]=crc>>8;
  feed(p,b,9+size);
}
int main(void)
{
  PulseParser parser={0};uint8_t duty[8];
  const uint8_t hello[]={0x50,0x4c,1,1,1,0,0,0xd9,0xfa};
  const uint8_t ack[]={0x50,0x4c,1,0x81,1,0,1,0,0xb4,0x86};
  uint8_t set[]={0x50,0x4c,1,2,2,0,5,0,0x88,0x13,0x50,0x64,0xe9,0xe4};
  assert(Pulse_Crc((const uint8_t*)"123456789",9)==0x29b1);
  feed(&parser,hello,3);assert(replies==0);feed(&parser,hello+3,6);
  assert(replies==1 && !memcmp(response,ack,10) && !effect.active);
  feed(&parser,set,sizeof(set));assert(sets==1 && effect.active && effect.value==5000);
  now=600;PulseEffect_Duty(&effect,now,duty);assert(duty[2]==64 && duty[1]==0 && duty[7]==0);
  uint32_t phase=effect.phase;
  feed(&parser,set,sizeof(set));assert(effect.phase==phase);
  set[9]^=1;feed(&parser,set,sizeof(set));assert(sets==2);set[9]^=1;
  uint8_t bad[]={3,0,0,80,100};packet(&parser,2,bad,5);assert(response[7]==1 && sets==2);
  bad[0]=0;bad[1]=0x11;bad[2]=0x27;packet(&parser,2,bad,5);assert(response[7]==1);
  bad[1]=0;bad[2]=0;bad[3]=101;packet(&parser,2,bad,5);assert(response[7]==1);
  bad[3]=80;bad[4]=49;packet(&parser,2,bad,5);assert(response[7]==1);
  packet(&parser,2,bad,4);assert(response[7]==1);
  packet(&parser,4,NULL,0);assert(response[3]==0x84 && response[7]==3);
  uint8_t noise[]={0,0x50,0x50,0x4c,1,1,0,0,33};feed(&parser,noise,sizeof(noise));
  feed(&parser,hello,sizeof(hello));assert(!memcmp(response,ack,10));
  feed(&parser,set,8);now+=1001;feed(&parser,hello,sizeof(hello));assert(!memcmp(response,ack,10));
  now=3601;PulseEffect_Duty(&effect,now,duty);assert(!effect.active);
  for(unsigned i=0;i<8;++i) assert(!duty[i]);
  now=0xfffffff0;PulseEffect_Set(&effect,now,10000,50,100);
  now+=30;PulseEffect_Duty(&effect,now,duty);assert(effect.active && duty[2]==40 && duty[6]==50);
  PulseEffect_Set(&effect,now,5000,100,200);phase=effect.phase;
  now+=100;PulseEffect_Duty(&effect,now,duty);assert(effect.phase==phase+20000);
  packet(&parser,3,NULL,0);assert(stops==1 && !effect.active && response[3]==0x83);
  puts("PASS: Pulse packets, CRC, fragmentation, resync, limits, timeout, wrap, continuous phase");
}
