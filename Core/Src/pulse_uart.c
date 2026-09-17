#include "pulse_uart.h"
#include "pulse_protocol.h"
#include "led_595.h"

static UART_HandleTypeDef *s_uart;
static uint8_t s_byte;
static volatile uint8_t s_ring[256];
static volatile uint16_t s_head, s_tail;
static volatile bool s_lost;
static bool s_polling;
static PulseParser s_parser;

bool PulseUART_Init(UART_HandleTypeDef *uart)
{
  s_uart=uart;
  s_head=s_tail=0;
  s_lost=false;
  s_parser.used=0;
  return HAL_UART_Receive_IT(uart,&s_byte,1)==HAL_OK;
}
void PulseUART_OnRx(UART_HandleTypeDef *uart)
{
  if (uart!=s_uart) return;
  uint16_t next=(s_head+1U)&255U;
  if (next==s_tail) s_lost=true;
  else { s_ring[s_head]=s_byte; s_head=next; }
  (void)HAL_UART_Receive_IT(uart,&s_byte,1);
}
void PulseUART_OnError(UART_HandleTypeDef *uart)
{
  if (uart!=s_uart) return;
  s_lost=true;
  /* HAL ends RX on overrun; Receive_IT then resets errors and rearms RX. */
  (void)HAL_UART_Receive_IT(uart,&s_byte,1);
}
static uint8_t Command(uint8_t type, const uint8_t *data)
{
  if (type==2)
    LED595_SetComputer((uint16_t)data[1]|((uint16_t)data[2]<<8),data[3],data[4]);
  else if (type==3) LED595_StopComputer();
  return 0;
}
static void Send(const uint8_t *bytes, uint8_t size)
{
  (void)HAL_UART_Transmit(s_uart,(uint8_t *)bytes,size,10U);
}
void PulseUART_Poll(void)
{
  if (!s_uart || s_polling || __get_IPSR()!=0U) return;
  s_polling=true;
  if (s_uart->RxState==HAL_UART_STATE_READY)
    (void)HAL_UART_Receive_IT(s_uart,&s_byte,1);
  /* Bound each service so malformed traffic cannot starve other tasks. */
  for (unsigned count=0;count<256;++count)
  {
    uint32_t saved=__get_PRIMASK();
    __disable_irq();
    if (s_lost)
    {
      s_tail=s_head;
      s_parser.used=0;
      s_lost=false;
    }
    bool available=s_tail!=s_head;
    uint8_t byte=0;
    if (available) { byte=s_ring[s_tail]; s_tail=(s_tail+1U)&255U; }
    __set_PRIMASK(saved);
    if (!available) break;
    PulseParser_Byte(&s_parser,byte,HAL_GetTick(),Command,Send);
  }
  s_polling=false;
}
