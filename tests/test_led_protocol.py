"""Compile the actual LED packet handler with native mocks; verify HMAC with Python.
Run: python tests/test_led_protocol.py (requires native gcc).
"""
from pathlib import Path
import hashlib
import hmac
import subprocess

root = Path(__file__).resolve().parents[1]
source = (root / 'Core/Src/image_transfer.c').read_text()
handler = source[source.index('static void HandleLed('):source.index('static void HandleOtaQuery(')]
key = bytes(range(32))
proof = hmac.new(key, b'EPD-LED-SET-V1' + bytes([0x34,0x12,2]), hashlib.sha256).digest()[:16]
code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "stm32g0xx_hal.h"
typedef int UART_HandleTypeDef;
static struct {uint32_t VAL;} mock_tick;
#define SysTick (&mock_tick)
uint32_t HAL_GetUIDw0(void) {return 1;}
uint32_t HAL_GetUIDw1(void) {return 2;}
uint32_t HAL_GetUIDw2(void) {return 3;}
uint32_t HAL_GetTick(void) {return 1234;}
#include "pairing_security.c"
#include "image_transfer.h"
#include "led_595.h"
static struct {bool authorized; ImageTransfer_State state; uint32_t last_activity_tick;} s_transfer;
static uint8_t response[35];
static unsigned packets, changes;
static ImageTransfer_Result ack;
static LED_Mode selected=LED_MODE_OFF;
static void SendAck(uint16_t seq,uint8_t type,ImageTransfer_Result result)
{(void)seq;(void)type;ack=result;}
static bool SendPacket(uint8_t type,uint16_t seq,const uint8_t *p,uint16_t size)
{assert(type==0x86 && seq==0x1234 && size==35);memcpy(response,p,35);++packets;return true;}
static void WriteU32(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(i*8));}
bool LED595_SetMode(LED_Mode mode) {selected=mode;++changes;return true;}
void LED595_GetStatus(LED595_Status *s)
{memset(s,0,sizeof(*s));s->mode=selected;s->effective_mode=3;s->humidity_valid=true;s->humidity=6400;s->elapsed_ms=1500;LED_Effect(1500,true,selected,true,6400,s->duty);}
'''
code += handler
code += '\nstatic const uint8_t golden[16]={' + ','.join(map(str,proof)) + '};\n'
code += r'''
int main(void)
{
  uint8_t payload[17]={2};
  for(unsigned i=0;i<32;++i) s_session_key[i]=(uint8_t)i;
  s_session_valid=true;
  memcpy(payload+1,golden,16);
  assert(PairingSecurity_VerifyLedSet(0x1234,2,golden));
  assert(!PairingSecurity_VerifyLedSet(0x1235,2,golden));
  assert(!PairingSecurity_VerifyLedSet(0x1234,1,golden));
  HandleLed(0x1234,IMAGE_MSG_LED_SET,payload,17);
  assert(ack==IMAGE_RESULT_NOT_AUTHORIZED && changes==0);
  s_transfer.authorized=true;s_transfer.state=IMAGE_STATE_RECEIVING;
  HandleLed(0x1234,IMAGE_MSG_LED_SET,payload,17);
  assert(ack==IMAGE_RESULT_BUSY && changes==0);
  s_transfer.state=IMAGE_STATE_AUTHENTICATED;
  HandleLed(0x1234,IMAGE_MSG_LED_SET,payload,1);
  assert(ack==IMAGE_RESULT_BAD_SIZE && changes==0);
  payload[0]=3;HandleLed(0x1234,IMAGE_MSG_LED_SET,payload,17);
  assert(ack==IMAGE_RESULT_BAD_PACKET && changes==0);
  payload[0]=2;payload[1]^=1;HandleLed(0x1234,IMAGE_MSG_LED_SET,payload,17);
  assert(ack==IMAGE_RESULT_AUTH_FAILED && changes==0);
  payload[1]^=1;HandleLed(0x1234,IMAGE_MSG_LED_SET,payload,17);
  assert(changes==1 && selected==LED_MODE_SPREAD && packets==1);
  assert(s_transfer.last_activity_tick==1234);
  HandleLed(0x1234,IMAGE_MSG_LED_GET,NULL,0);
  assert(changes==1 && packets==2);
  HandleLed(0x1234,IMAGE_MSG_LED_GET,payload,1);
  assert(ack==IMAGE_RESULT_BAD_SIZE && packets==2);
  for(unsigned i=0;i<35;++i) printf("%02x",response[i]);
  puts("");
  PairingSecurity_ClearSession();
  assert(!PairingSecurity_VerifyLedSet(0x1234,2,golden));
}
'''
build = root / 'tmp'
build.mkdir(exist_ok=True)
(build/'test_led_protocol.c').write_text(code)
subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-Itests/led_stubs','-ICore/Inc','-ICore/Src',str(build/'test_led_protocol.c'),'-o',str(build/'test_led_protocol.exe')],cwd=root,check=True)
response = bytes.fromhex(subprocess.check_output([str(build/'test_led_protocol.exe')],cwd=root,text=True).strip())
assert response[:4] == bytes([0,2,3,1])
assert int.from_bytes(response[4:8],'little') == 6400
assert int.from_bytes(response[8:12],'little') == 1500
assert response[12:19] == bytes([80,80,100,100,100,100,80])
expected = hmac.new(key,b'EPD-LED-STATE-V1'+bytes([0x34,0x12])+response[:19],hashlib.sha256).digest()[:16]
assert response[19:] == expected
print('PASS: actual LED handler, auth/busy/length/mode rejection, HMAC tampering, SET/GET state, independent Python HMAC')
