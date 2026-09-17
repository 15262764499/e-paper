#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../Core/Src/pulse_uart.c"
uint32_t mock_ipsr;
static uint32_t tick;
static uint8_t *receive;
static unsigned sends, sets, stops, armed;
static uint8_t last_packet[32];
uint32_t HAL_GetTick(void) { return tick; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u,uint8_t *p,uint16_t n)
{ assert(n==1); receive=p;u->RxState=1;++armed;return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *u,uint8_t *p,uint16_t n,uint32_t timeout)
{ assert(u==s_uart && (n==10 || n==18) && timeout==10);
  memcpy(last_packet,p,n);++sends;PulseUART_Poll();return HAL_OK; }
void LED595_SetComputer(uint16_t v,uint8_t b,uint8_t s)
{assert(v==5000 && b==80 && s==100);++sets;}
void LED595_StopComputer(void) {++stops;}
static void input(UART_HandleTypeDef *u,const uint8_t *bytes,unsigned n)
{for(unsigned i=0;i<n;++i) {*receive=bytes[i];u->RxState=HAL_UART_STATE_READY;PulseUART_OnRx(u);}}
int main(void)
{
  UART_HandleTypeDef uart={0},other={0};
  const uint8_t hello[]={0x50,0x4c,1,1,1,0,0,0xd9,0xfa};
  const uint8_t set[]={0x50,0x4c,1,2,2,0,5,0,0x88,0x13,0x50,0x64,0xe9,0xe4};
  PulseUART_Poll();assert(!sends);
  assert(PulseUART_Init(&uart));assert(armed==1);
  input(&uart,hello,4);PulseUART_Poll();assert(!sends);
  input(&uart,hello+4,5);mock_ipsr=1;PulseUART_Poll();assert(!sends);
  mock_ipsr=0;PulseUART_Poll();assert(sends==1);
  input(&uart,set,sizeof(set));PulseUART_Poll();assert(sets==1 && sends==2);
  PulseUART_OnError(&other);assert(!s_lost);
  input(&uart,set,5);PulseUART_OnError(&uart);PulseUART_Poll();
  input(&uart,hello,sizeof(hello));PulseUART_Poll();assert(sends==3);
  for(unsigned i=0;i<30;++i) input(&uart,hello,sizeof(hello));
  assert(s_lost);PulseUART_Poll();assert(sends==3 && !s_lost);
  input(&uart,hello,sizeof(hello));PulseUART_Poll();assert(sends==4);
  uart.RxState=HAL_UART_STATE_READY;unsigned before=armed;PulseUART_Poll();assert(armed==before+1);
  assert(!stops);
  PulseUART_ReportEnvironment(0,-525,6412);
  assert(sends==5 && last_packet[3]==0x90 && last_packet[6]==9);
  assert(last_packet[7]==0 && last_packet[8]==0xf3 && last_packet[9]==0xfd);
  assert(last_packet[10]==0xff && last_packet[11]==0xff);
  assert(last_packet[12]==0x0c && last_packet[13]==0x19);
  assert(Pulse_Crc(last_packet+2,14)==(last_packet[16]|((uint16_t)last_packet[17]<<8)));
  mock_ipsr=1;PulseUART_ReportEnvironment(0,1234,5678);assert(sends==5);
  puts("PASS: actual UART ring, fragmented frames, IRQ guard, reentry, overrun/error recovery, RX rearm");
}
