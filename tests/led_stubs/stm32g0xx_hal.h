#ifndef STM32G0XX_HAL_H
#define STM32G0XX_HAL_H
#include <stdint.h>
#include <stddef.h>
#define __IO volatile
#define __NOP() ((void)0)
#define __disable_irq() ((void)0)
#define __get_PRIMASK() 0U
#define __set_PRIMASK(x) ((void)(x))
extern uint32_t mock_ipsr;
#define __get_IPSR() mock_ipsr
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_SPI2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM1_CLK_ENABLE() ((void)0)
#define SPI_1LINE_TX(x) ((void)(x))
#define __HAL_SPI_ENABLE(x) ((void)(x))
#define __HAL_TIM_CLEAR_FLAG(x,y) ((void)(x),(void)(y))
typedef struct { uint32_t SR, DR; } SPI_TypeDef;
typedef struct { uint32_t SR, CR1, ARR, CNT; } TIM_TypeDef;
typedef struct { uint32_t BSRR, BRR; } GPIO_TypeDef;
typedef struct { uint32_t CFGR; } RCC_TypeDef;
extern SPI_TypeDef mock_spi;
extern TIM_TypeDef mock_tim;
extern GPIO_TypeDef mock_a, mock_b;
extern RCC_TypeDef mock_rcc;
#define SPI2 (&mock_spi)
#define TIM1 (&mock_tim)
#define GPIOA (&mock_a)
#define GPIOB (&mock_b)
#define RCC (&mock_rcc)
typedef struct { uint32_t Pin,Mode,Pull,Speed,Alternate; } GPIO_InitTypeDef;
typedef struct { SPI_TypeDef *Instance; struct { uint32_t Mode,Direction,DataSize,CLKPolarity,CLKPhase,NSS,BaudRatePrescaler,FirstBit,TIMode,CRCCalculation,CRCPolynomial,CRCLength,NSSPMode; } Init; } SPI_HandleTypeDef;
typedef struct { TIM_TypeDef *Instance; struct { uint32_t Prescaler,CounterMode,Period,ClockDivision,RepetitionCounter,AutoReloadPreload; } Init; } TIM_HandleTypeDef;
typedef struct { uint32_t State; } I2C_HandleTypeDef;
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
#define GPIO_PIN_0 1U
#define GPIO_PIN_4 16U
#define GPIO_PIN_8 256U
#define GPIO_PIN_12 4096U
#define GPIO_PIN_14 16384U
#define SPI_SR_TXE 2U
#define SPI_SR_BSY 128U
#define TIM_SR_UIF 1U
#define TIM_CR1_CEN 1U
#define RCC_CFGR_PPRE 256U
#define HAL_I2C_STATE_READY 0U
#define I2C_MEMADD_SIZE_16BIT 2U
#define GPIO_MODE_OUTPUT_PP 0U
#define GPIO_MODE_AF_PP 1U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_HIGH 0U
#define GPIO_AF0_SPI2 0U
#define GPIO_AF1_SPI2 1U
#define SPI_MODE_MASTER 0U
#define SPI_DIRECTION_1LINE 0U
#define SPI_DATASIZE_8BIT 0U
#define SPI_POLARITY_LOW 0U
#define SPI_PHASE_1EDGE 0U
#define SPI_NSS_SOFT 0U
#define SPI_BAUDRATEPRESCALER_8 0U
#define SPI_FIRSTBIT_MSB 0U
#define SPI_TIMODE_DISABLE 0U
#define SPI_CRCCALCULATION_DISABLE 0U
#define SPI_CRC_LENGTH_DATASIZE 0U
#define SPI_NSS_PULSE_DISABLE 0U
#define TIM_COUNTERMODE_UP 0U
#define TIM_CLOCKDIVISION_DIV1 0U
#define TIM_AUTORELOAD_PRELOAD_DISABLE 0U
#define TIM_FLAG_UPDATE 1U
#define TIM1_BRK_UP_TRG_COM_IRQn 0U
uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint32_t, GPIO_PinState);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *, uint32_t);
void HAL_GPIO_Init(GPIO_TypeDef *, GPIO_InitTypeDef *);
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *);
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *);
void HAL_NVIC_SetPriority(uint32_t,uint32_t,uint32_t);
void HAL_NVIC_EnableIRQ(uint32_t);
uint32_t HAL_I2C_GetState(I2C_HandleTypeDef *);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *,uint16_t,uint16_t,uint16_t,uint8_t *,uint16_t,uint32_t);
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *,uint16_t,uint8_t *,uint16_t,uint32_t);
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *,uint16_t,uint8_t *,uint16_t,uint32_t);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *,uint16_t,uint32_t,uint32_t);
void HAL_Delay(uint32_t);
#endif
