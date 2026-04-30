/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
//#include "bno080.h"
//#include "quaternion.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
int __io_putchar(int ch) {
       HAL_UART_Transmit(&huart3, (uint8_t*) &ch, 1, 0xFFFF);
       return ch;
}
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RAD_TO_DEG (180.0/M_PI)
//#define SCALE_Q(n) (1.0 / (1 << n))
#define SCALE_Q(n) (pow(0.5, n))
//#define BNO_I2C_ADDRESS (0x4A << 1) // Adafruit
#define BNO_I2C_ADDRESS (0x4B << 1) // SparkFun BNO086
#define BNO_I2C_HANDLE &hi2c1
#define BNO_READ_PERIOD 20 // ms

__attribute__((section(".axi_sram"), aligned(32), used)) uint8_t BnoRxBuff[BNO_MSG_LENGTH];

//uint8_t BnoRxBuff[BNO_MSG_LENGTH];
int16_t Data1;
int16_t Data2;
int16_t Data3;
int16_t Data4;

imu_t bno080_st;


char lcd_line[64];
uint32_t BnoSoftTimer;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint8_t START_BNO_STABILIZED_ROTATION_VECTOR_100_HZ[/* BNO_MSG_LENGTH */] =
{ 0x15, 0x00, 0x02, 0x00,
  0xFD,
  0x28,
  0x00, 0x00, 0x00,
  0x10, 0x27, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00 };
// Calibrated Accelerometer (0x01) - 100Hz = 10000us = 0x2710
uint8_t START_CALIBRATED_ACCEL_100_HZ[] = {
    0x15, 0x00, 0x02, 0x00,   // length, channel, seqnum
    0xFD,                      // Set Feature Command
    0x01,                      // Report ID: Calibrated Accelerometer
    0x00, 0x00, 0x00,
    0x10, 0x27, 0x00, 0x00,   // 10000us = 100Hz
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

// Calibrated Gyroscope (0x02) - 100Hz
uint8_t START_CALIBRATED_GYRO_100_HZ[] = {
    0x15, 0x00, 0x02, 0x00,
    0xFD,
    0x02,                      // Report ID: Calibrated Accelerometer
    0x00, 0x00, 0x00,
    0x10, 0x27, 0x00, 0x00,   // 10000us = 100Hz
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1)
    {
        // Invalidate D-Cache FIRST before reading buffer
        SCB_InvalidateDCache_by_Addr((uint32_t*)BnoRxBuff, BNO_MSG_LENGTH);

        uint16_t pkt_len = ((uint16_t)(BnoRxBuff[1] & 0x7F) << 8) | BnoRxBuff[0];
        uint8_t  channel = BnoRxBuff[2];

        if ((channel == 2 || channel == 3) && pkt_len > 4)
        {
            bno080_st.rx_flag_u8 = 1;
            BSP_LED_Toggle(LED_YELLOW);
        }
        HAL_I2C_Master_Receive_DMA(BNO_I2C_HANDLE, BNO_I2C_ADDRESS,
                                   BnoRxBuff, BNO_MSG_LENGTH);
    }
}

void bno080_init(void)
{
	HAL_I2C_Master_Transmit(BNO_I2C_HANDLE, BNO_I2C_ADDRESS,
			START_BNO_STABILIZED_ROTATION_VECTOR_100_HZ,
			sizeof(START_BNO_STABILIZED_ROTATION_VECTOR_100_HZ), 10);
	HAL_Delay(100);
  HAL_I2C_Master_Transmit(BNO_I2C_HANDLE, BNO_I2C_ADDRESS,
			START_CALIBRATED_ACCEL_100_HZ,
			sizeof(START_CALIBRATED_ACCEL_100_HZ), 10);  
	HAL_Delay(100);
  HAL_I2C_Master_Transmit(BNO_I2C_HANDLE, BNO_I2C_ADDRESS,
			START_CALIBRATED_GYRO_100_HZ,
			sizeof(START_CALIBRATED_GYRO_100_HZ), 10);        
	HAL_Delay(100);
  HAL_I2C_Master_Receive_DMA(BNO_I2C_HANDLE, BNO_I2C_ADDRESS, BnoRxBuff,
	BNO_MSG_LENGTH);
}

void process_imu_rx(imu_t *p_imu_st)
{
    // Safe snapshot after cache has been invalidated in callback
    memcpy(p_imu_st->pkt, BnoRxBuff, BNO_MSG_LENGTH);
    int32_t Qi, Qj, Qk, Qr;
    uint8_t channel   = p_imu_st->pkt[2];
    uint8_t report_id = p_imu_st->pkt[9];  // byte 9 confirmed correct from your buffer

    if (channel != 2 && channel != 3) return;

    // All reports: data bytes start at [13], little-endian int16
    int16_t raw1 = (int16_t)(((uint16_t)p_imu_st->pkt[14] << 8) | p_imu_st->pkt[13]);
    int16_t raw2 = (int16_t)(((uint16_t)p_imu_st->pkt[16] << 8) | p_imu_st->pkt[15]);
    int16_t raw3 = (int16_t)(((uint16_t)p_imu_st->pkt[18] << 8) | p_imu_st->pkt[17]);
    int16_t raw4 = (int16_t)(((uint16_t)p_imu_st->pkt[20] << 8) | p_imu_st->pkt[19]);

    if (report_id == 0x01)  // Calibrated Accelerometer — Q8 = 1/256 m/s²
    {
        p_imu_st->accel_st.x_i32 = 1000 * (float)raw1 * SCALE_Q(8);
        p_imu_st->accel_st.y_i32 = 1000 * (float)raw2 * SCALE_Q(8);
        p_imu_st->accel_st.z_i32 = 1000 * (float)raw3 * SCALE_Q(8);
    }
    else if (report_id == 0x02)  // Calibrated Gyroscope — Q9 = 1/512 rad/s
    {
        p_imu_st->gyro_st.x_i32 = 1000 * (float)raw1 * SCALE_Q(9);
        p_imu_st->gyro_st.y_i32 = 1000 * (float)raw2 * SCALE_Q(9);
        p_imu_st->gyro_st.z_i32 = 1000 * (float)raw3 * SCALE_Q(9);
    }
    else if (report_id == 0x28)  // ARVR-Stabilized Rotation Vector — Q14
    {
        Qr = 1000 * (float)raw1 * SCALE_Q(14);
        Qi = 1000 * (float)raw2 * SCALE_Q(14);
        Qj = 1000 * (float)raw3 * SCALE_Q(14);
        Qk = 1000 * (float)raw4 * SCALE_Q(14);

        int16_t norm = Qi*Qi + Qj*Qj + Qk*Qk + Qr*Qr;
        if (norm < 0.001) return;

        p_imu_st->vector_st.x_i32   = atan2(2.0*(Qi*Qj + Qk*Qr),
                     (Qi*Qi - Qj*Qj - Qk*Qk + Qr*Qr)) * RAD_TO_DEG;
        p_imu_st->vector_st.y_i32 = asin(-2.0*(Qi*Qk - Qj*Qr) / norm) * RAD_TO_DEG;
        p_imu_st->vector_st.z_i32  = atan2(2.0*(Qj*Qk + Qi*Qr),
                     (-Qi*Qi - Qj*Qj + Qk*Qk + Qr*Qr)) * RAD_TO_DEG;
    }
}

void imu_print(imu_t *p_imu_st)
{
  printf("Accel X:%ld Y:%ld Z:%ld mm/s2 Gyro X:%ld Y:%ld Z:%ld mrad/s Yaw %ld Pitch %ld Roll %ld\r\n",
  p_imu_st->accel_st.x_i32, p_imu_st->accel_st.y_i32, p_imu_st->accel_st.z_i32, p_imu_st->gyro_st.x_i32, p_imu_st->gyro_st.y_i32, p_imu_st->gyro_st.z_i32,
  p_imu_st->vector_st.x_i32, p_imu_st->vector_st.y_i32, p_imu_st->vector_st.z_i32);
}


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_DMA_Init();
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_SPI2_Init();
  MX_TIM3_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  bno080_init();
  BnoSoftTimer = HAL_GetTick();
  printf("init ok");
  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_YELLOW);
  BSP_LED_Init(LED_RED);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (HAL_GetTick() - BnoSoftTimer > BNO_READ_PERIOD)
		{
			BnoSoftTimer = HAL_GetTick();

      if(bno080_st.rx_flag_u8)
      {
        bno080_st.rx_flag_u8 = 0;
        process_imu_rx(&bno080_st);
        imu_print(&bno080_st);
      }
    }
  
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = 64;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 34;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 3;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 3072;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{

  /* Disables the MPU */
  LL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  LL_MPU_ConfigRegion(LL_MPU_REGION_NUMBER0, 0x87, 0x0, LL_MPU_REGION_SIZE_4GB|LL_MPU_TEX_LEVEL0|LL_MPU_REGION_NO_ACCESS|LL_MPU_INSTRUCTION_ACCESS_DISABLE|LL_MPU_ACCESS_SHAREABLE|LL_MPU_ACCESS_NOT_CACHEABLE|LL_MPU_ACCESS_NOT_BUFFERABLE);

  /** Initializes and configures the Region and the memory to be protected
  */
  LL_MPU_ConfigRegion(LL_MPU_REGION_NUMBER1, 0x0, 0x30000000, LL_MPU_REGION_SIZE_8KB|LL_MPU_TEX_LEVEL0|LL_MPU_REGION_NO_ACCESS|LL_MPU_INSTRUCTION_ACCESS_ENABLE|LL_MPU_ACCESS_SHAREABLE|LL_MPU_ACCESS_NOT_CACHEABLE|LL_MPU_ACCESS_NOT_BUFFERABLE);

  /** Initializes and configures the Region and the memory to be protected
  */
  LL_MPU_ConfigRegion(LL_MPU_REGION_NUMBER2, 0x0, 0x24000000, LL_MPU_REGION_SIZE_16KB|LL_MPU_TEX_LEVEL1|LL_MPU_REGION_FULL_ACCESS|LL_MPU_INSTRUCTION_ACCESS_ENABLE|LL_MPU_ACCESS_NOT_SHAREABLE|LL_MPU_ACCESS_NOT_CACHEABLE|LL_MPU_ACCESS_NOT_BUFFERABLE);
  /* Enables the MPU */
  LL_MPU_Enable(LL_MPU_CTRL_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
