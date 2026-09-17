/* Native regression: gcc -std=c11 -Wall -Wextra -Werror -Itests/led_stubs
 * -ICore/Inc tests/test_led_595.c -o tmp/test_led_595.exe */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../Core/Src/led_595.c"

SPI_TypeDef mock_spi = {.SR = SPI_SR_TXE};
TIM_TypeDef mock_tim;
GPIO_TypeDef mock_a, mock_b;
RCC_TypeDef mock_rcc;
uint32_t mock_ipsr;
static uint32_t tick, nfc_reads, sensor_begins, sensor_finishes;
static GPIO_PinState key = GPIO_PIN_SET;
static uint8_t field;
static HAL_StatusTypeDef nfc_result = HAL_OK;
static AHT20_Status sensor_result = AHT20_OK;
static unsigned environment_reports;
void PulseUART_ReportEnvironment(uint8_t status,int32_t temperature,uint32_t humidity)
{ ++environment_reports;assert(status==(uint8_t)sensor_result);
  if(status==0) {assert(temperature==2534);assert(humidity==6400);} }
uint32_t HAL_GetTick(void) { return tick; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return 16000000U; }
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint32_t b,GPIO_PinState v) {(void)p;(void)b;(void)v;}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p,uint32_t b) {(void)p;(void)b;return key;}
void HAL_GPIO_Init(GPIO_TypeDef *p,GPIO_InitTypeDef *g) {(void)p;(void)g;}
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *p) {(void)p;return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *p) {(void)p;return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *p) {(void)p;return HAL_OK;}
void HAL_NVIC_SetPriority(uint32_t a,uint32_t b,uint32_t c) {(void)a;(void)b;(void)c;}
void HAL_NVIC_EnableIRQ(uint32_t a) {(void)a;}
uint32_t HAL_I2C_GetState(I2C_HandleTypeDef *p) {return p->State;}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *p,uint16_t a,uint16_t r,uint16_t z,uint8_t *v,uint16_t n,uint32_t t)
{(void)p;assert(a==(0x53U<<1));assert(r==0x2002U);assert(z==2U);assert(n==1U);assert(t==5U);++nfc_reads;*v=field;return nfc_result;}
AHT20_Status AHT20_BeginMeasurement(I2C_HandleTypeDef *p)
{(void)p;++sensor_begins;return sensor_result;}
AHT20_Status AHT20_FinishMeasurement(I2C_HandleTypeDef *p,AHT20_Measurement *m)
{(void)p;++sensor_finishes;m->temperature_centi_c=2534;m->humidity_centi_percent=6400U;return sensor_result;}
static void advance(uint32_t ms) {while(ms--) {++tick;LED595_Tick();}}
static void press(void) {key=GPIO_PIN_RESET;advance(31);key=GPIO_PIN_SET;advance(31);}
static void check_frame(const uint8_t duty[8])
{
  PublishDuty(duty);
  assert(s_pending.count>=1 && s_pending.count<=8);
  uint32_t total=0, on[8]={0};
  for (uint32_t i=0;i<s_pending.count;++i)
  {
    assert(s_pending.duration[i]>0);
    assert((s_pending.mask[i]&1U)==0U);
    total+=s_pending.duration[i];
    for (uint32_t q=1;q<=7;++q)
      if(s_pending.mask[i]&(1U<<q)) on[q]+=s_pending.duration[i];
  }
  assert(total==4000U);
  for(uint32_t q=1;q<=7;++q) assert(on[q]==duty[q]*40U);
}
int main(void)
{
  uint8_t duty[8], other[8];
  const uint32_t humidities[]={0,1,1599,1600,1601,3200,4800,6400,7999,8000,8001,10000};
  for(uint32_t h=0;h<sizeof(humidities)/sizeof(humidities[0]);++h)
    for(uint32_t t=0;t<3000;t+=10)
    {
      LED_Effect(t,false,true,true,humidities[h],duty);
      assert(duty[0]==0 && duty[1]==0 && duty[7]==0);
      for(uint32_t q=2;q<=6;++q)
      {
        assert(duty[q]<=(q==2?80:100));
        if(humidities[h]<=1600U*(q-2U)) assert(duty[q]==0);
        if(humidities[h]>=8000) assert(duty[q]==(q==2?80:100));
      }
      check_frame(duty);
      LED_Effect(t,true,false,false,0,duty);
      LED_Effect(t,true,true,true,humidities[h],other);
      assert(memcmp(duty,other,8)==0); /* NFC overrides every background. */
      assert(duty[1]==duty[2] && duty[2]==duty[7]);
      assert(duty[1]<=80 && duty[3]<=100);
      check_frame(duty);
      LED_Effect(t,false,false,true,humidities[h],duty);
      for(uint32_t q=0;q<8;++q) assert(duty[q]==0);
      LED_Effect(t,false,true,false,humidities[h],duty);
      for(uint32_t q=0;q<8;++q) assert(duty[q]==0);
    }
  for(uint32_t q=2;q<=6;++q)
  {
    LED_Effect(600U+(q-2U)*300U,false,true,true,7999U,duty);
    assert(duty[q]>=(q==2?80:99)); /* Forward wave peaks. */
  }
  for(uint32_t t=0;t<=2000;t+=10)
  {
    LED_Effect(t,false,LED_MODE_SPREAD,false,0,duty);
    check_frame(duty);
    for(unsigned q=1;q<=7;++q)
    {
      unsigned dist=q>4?q-4:4-q;
      if(t<=dist*300) assert(duty[q]==0);
      if(t>=dist*300+600) assert(duty[q]==((q==1||q==2||q==7)?80:100));
    }
    LED_Effect(t,true,LED_MODE_SPREAD,false,0,duty);
    LED_Effect(t,true,LED_MODE_OFF,false,0,other);
    assert(memcmp(duty,other,8)==0);
  }
  for(uint32_t t=1500;t<=2500;t+=10)
  {
    LED_Effect(t,false,LED_MODE_SPREAD,false,0,duty);
    for(unsigned q=1;q<=7;++q) assert(duty[q]==((q==1||q==2||q==7)?80:100));
  }
  LED_Effect(2500,false,LED_MODE_SPREAD,false,0,other);
  for(uint32_t t=2510;t<=3100;t+=10)
  {
    LED_Effect(t,false,LED_MODE_SPREAD,false,0,duty);
    for(unsigned q=1;q<=7;++q) assert(duty[q]<=other[q]);
    check_frame(duty);
    memcpy(other,duty,8);
  }
  for(unsigned q=0;q<8;++q) assert(duty[q]==0);
  LED_Effect(2800,false,LED_MODE_SPREAD,false,0,duty);
  assert(duty[1]==20 && duty[4]==25);
  for(uint32_t t=0;t<3100;t+=10)
  {
    LED_Effect(t,false,LED_MODE_SPREAD,false,0,duty);
    LED_Effect(t+3100U*10U,false,LED_MODE_SPREAD,false,0,other);
    assert(memcmp(duty,other,8)==0);
  }
  I2C_HandleTypeDef bus={0};
  assert(LED595_Init());
  LED595_AttachI2C(&bus);
  assert(s_mode == LED_MODE_OFF && !s_nfc);
  key=GPIO_PIN_RESET;advance(15);key=GPIO_PIN_SET;advance(31);
  assert(s_mode == LED_MODE_OFF); /* Bounce cannot toggle. */
  key=GPIO_PIN_RESET;advance(31);assert(s_mode == LED_MODE_HUMIDITY);
  advance(2000);assert(s_mode == LED_MODE_HUMIDITY); /* No long-press repeat. */
  key=GPIO_PIN_SET;advance(31);
  LED595_Poll();assert(sensor_begins==1 && sensor_finishes==0);
  advance(79);LED595_Poll();assert(sensor_finishes==0);
  advance(1);LED595_Poll();assert(sensor_finishes==1 && s_humidity==6400);
  field=NFC_FIELD_ON;advance(40);LED595_PollNfc();assert(s_nfc);
  press();assert(s_mode == LED_MODE_HUMIDITY); /* Key ignored during NFC. */
  LED595_Status nfc_state;
  assert(LED595_SetMode(LED_MODE_SPREAD));
  LED595_GetStatus(&nfc_state);
  assert(nfc_state.mode==2 && nfc_state.effective_mode==3);
  assert(LED595_SetMode(LED_MODE_HUMIDITY));
  field=0;advance(80);LED595_PollNfc();assert(s_nfc);
  advance(300);LED595_PollNfc();assert(!s_nfc && s_mode == LED_MODE_HUMIDITY);
  press();assert(s_mode == LED_MODE_SPREAD);
  advance(1600);
  LED595_Status state;
  LED595_GetStatus(&state);
  assert(state.mode==2 && state.effective_mode==2 && state.elapsed_ms==((tick-s_started)%3100U));
  for(unsigned q=1;q<=7;++q) assert(state.duty[q]==((q==1||q==2||q==7)?80:100));
  advance(1500);
  LED595_GetStatus(&state);
  assert(state.elapsed_ms==((tick-s_started)%3100U));
  uint8_t expected[8];
  LED_Effect(state.elapsed_ms,false,LED_MODE_SPREAD,false,0,expected);
  assert(memcmp(state.duty,expected,8)==0);
  uint32_t started=s_started;
  assert(LED595_SetMode(LED_MODE_SPREAD) && s_started==started);
  assert(!LED595_SetMode((LED_Mode)3) && s_mode==LED_MODE_SPREAD);
  press();assert(s_mode == LED_MODE_OFF);
  field=NFC_FIELD_ON;advance(40);LED595_PollNfc();assert(s_nfc);
  nfc_result=HAL_ERROR;advance(301);LED595_PollNfc();assert(!s_nfc);
  uint32_t reads=nfc_reads;
  bus.State=1;advance(40);LED595_PollNfc();assert(nfc_reads==reads);
  bus.State=0;mock_ipsr=1;LED595_PollNfc();assert(nfc_reads==reads);mock_ipsr=0;
  /* Wrap-safe debounce and RF release. */
  tick=UINT32_MAX-20U;key=GPIO_PIN_RESET;advance(31);assert(s_mode == LED_MODE_HUMIDITY);
  key=GPIO_PIN_SET;advance(31);
  field=NFC_FIELD_ON;nfc_result=HAL_OK;LED595_PollNfc();assert(s_nfc);
  field=0;advance(301);LED595_PollNfc();assert(!s_nfc);
  LED595_SetComputer(10000,50,100);advance(10);
  LED595_GetStatus(&state);
  assert(state.effective_mode==4 && state.duty[2]==40 && state.duty[6]==50);
  field=NFC_FIELD_ON;advance(40);LED595_PollNfc();LED595_GetStatus(&state);
  assert(state.effective_mode==3);
  field=0;advance(301);LED595_PollNfc();LED595_GetStatus(&state);
  assert(state.effective_mode==4 && state.duty[6]==50);
  advance(3001);LED595_GetStatus(&state);
  assert(state.effective_mode==LED_MODE_OFF);
  for(unsigned q=0;q<8;++q) assert(state.duty[q]==0);
  LED595_SetComputer(5000,80,100);advance(600);
  LED595_GetStatus(&state);assert(state.duty[2]==64);
  LED595_SetComputer(5000,80,100);LED595_GetStatus(&state);assert(state.duty[2]==64);
  LED595_StopComputer();advance(10);LED595_GetStatus(&state);
  assert(state.effective_mode==LED_MODE_OFF);
  LED595_SetComputer(10000,100,100);press();LED595_GetStatus(&state);
  assert(state.effective_mode==LED_MODE_HUMIDITY);
  /* Actual IRQ consumes a published frame, holds it until next frame. */
  LED_Effect(1500,true,false,false,0,duty);PublishDuty(duty);
  s_slot=0;mock_tim.SR=TIM_SR_UIF;LED595_TimerIRQ();
  assert((uint8_t)mock_spi.DR==1U && mock_tim.ARR==3199U);
  mock_tim.SR=TIM_SR_UIF;LED595_TimerIRQ();
  assert((uint8_t)mock_spi.DR==(uint8_t)~0x78U && mock_tim.ARR==799U);
  /* Busy SPI fails bounded and leaves OE blank. */
  mock_spi.SR=SPI_SR_BSY;assert(!ShiftMask(0xFE));
  /* Sensor streaming continues while computer lights own the board. */
  LED595_SetComputer(5000,80,100);
  unsigned reported=environment_reports;
  advance(SENSOR_POLL_MS);LED595_Poll();
  assert(s_sensor_pending);
  advance(80);LED595_Poll();assert(environment_reports==reported+1);
  sensor_result=AHT20_CRC_ERROR;
  advance(SENSOR_POLL_MS);LED595_Poll();
  assert(!s_sensor_pending && environment_reports==reported+2);
  puts("PASS: effects, PWM integration, key debounce/hold, NFC priority/release/errors, split-phase sample, tick wrap, IRQ masks");
  return 0;
}
