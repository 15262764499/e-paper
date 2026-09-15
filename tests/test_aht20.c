#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../Core/Src/aht20.c"
static uint32_t tick, writes;
static uint8_t sample[7]={0x18,0x80,0,0,0,0,0};
static HAL_StatusTypeDef result=HAL_OK;
uint32_t HAL_GetTick(void) {return tick;}
void HAL_Delay(uint32_t ms) {tick+=ms;}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *p,uint16_t a,uint32_t n,uint32_t t)
{(void)p;(void)a;(void)n;(void)t;return result;}
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *p,uint16_t a,uint8_t *v,uint16_t n,uint32_t t)
{(void)p;(void)t;assert(a==(0x38U<<1));assert(n==1||n==7);memcpy(v,sample,n);return result;}
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *p,uint16_t a,uint8_t *v,uint16_t n,uint32_t t)
{(void)p;(void)t;assert(a==(0x38U<<1));assert(n==3);assert(v[0]==0xAC&&v[1]==0x33&&v[2]==0);++writes;return result;}
int main(void)
{
  I2C_HandleTypeDef bus={0};
  AHT20_Measurement m, blocking;
  sample[6]=AHT20_CalculateCrc(sample,6);
  assert(AHT20_BeginMeasurement(&bus)==AHT20_OK);
  assert(AHT20_FinishMeasurement(&bus,&m)==AHT20_OK);
  assert(tick==0 && writes==1); /* No conversion delay in either split phase. */
  assert(m.humidity_centi_percent==5000 && m.temperature_centi_c==-5000);
  assert(AHT20_ReadMeasurement(&bus,&blocking)==AHT20_OK);
  assert(blocking.humidity_centi_percent==m.humidity_centi_percent);
  assert(blocking.temperature_centi_c==m.temperature_centi_c);
  sample[0]|=0x80;
  assert(AHT20_BeginMeasurement(&bus)==AHT20_BUSY);
  assert(AHT20_FinishMeasurement(&bus,&m)==AHT20_BUSY);
  sample[0]=0x18;sample[6]^=1;
  assert(AHT20_FinishMeasurement(&bus,&m)==AHT20_CRC_ERROR);
  sample[0]=0;
  assert(AHT20_BeginMeasurement(&bus)==AHT20_NOT_CALIBRATED);
  result=HAL_ERROR;
  assert(AHT20_BeginMeasurement(&bus)==AHT20_I2C_ERROR);
  assert(AHT20_FinishMeasurement(&bus,&m)==AHT20_I2C_ERROR);
  assert(AHT20_BeginMeasurement(NULL)==AHT20_INVALID_ARGUMENT);
  assert(AHT20_FinishMeasurement(&bus,NULL)==AHT20_INVALID_ARGUMENT);
  puts("PASS: split-phase AHT20, command, decode compatibility, CRC/busy/calibration/I2C failures");
}
