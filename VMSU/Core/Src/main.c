/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
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
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "string.h"
#include "logger.h"
#include "stdio.h"
#include <math.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#define WINDOW 10


/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

SPI_HandleTypeDef hspi2;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
static logger_t g_logger;

uint16_t values_adc[2];
float oil_pressure;
float coolant_temp;
float coolant_voltage;
float pressure_voltage;

// --- Nextion RX parser (expects: 'S' '0-2' '=' + 4-byte little endian val) ---
static uint8_t nx_rx_byte;

typedef enum {
  NX_WAIT_S,
  NX_WAIT_ID,
  NX_WAIT_EQ,
  NX_VAL0,
  NX_VAL1,
  NX_VAL2,
  NX_VAL3
} NX_State_t;

static NX_State_t nx_state = NX_WAIT_S;
static uint8_t nx_switch_id = 0;
static uint32_t nx_val = 0;


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_SPI2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#define SIG1_GPIO_Port GPIOB
#define SIG1_Pin       GPIO_PIN_3

#define SIG2_GPIO_Port GPIOB
#define SIG2_Pin       GPIO_PIN_4

#define SIG3_GPIO_Port GPIOB
#define SIG3_Pin       GPIO_PIN_5

// ---------------------------------------------


static void sd_basic_test(void)
{
  DSTATUS st = disk_initialize(0);

  FRESULT r_mount, r_open, r_write, r_sync, r_close;
  FIL file;
  UINT bw = 0;

  r_mount = f_mount(&USERFatFS, (TCHAR const*)USERPath, 1);

  char path[32];
  sprintf(path, "%s/module.txt", USERPath);   // yields "0:/test.txt"

  r_open  = (r_mount == FR_OK) ? f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE) : r_mount;
  r_write = (r_open  == FR_OK) ? f_write(&file, "Hello from STM32!\r\n", 19, &bw) : r_open;
  r_sync  = (r_open  == FR_OK) ? f_sync(&file) : r_open;
  r_close = (r_open  == FR_OK) ? f_close(&file) : r_open;

  // Put breakpoint here and inspect:
  // st, r_mount, r_open, r_write, r_sync, r_close, bw
  (void)st; (void)r_mount; (void)r_open; (void)r_write; (void)r_sync; (void)r_close;
}

static void ApplySwitch(uint8_t id, uint32_t v)
{
  GPIO_PinState ps = (v ? GPIO_PIN_SET : GPIO_PIN_RESET);

  switch (id)
  {
    case 0: HAL_GPIO_WritePin(SIG1_GPIO_Port, SIG1_Pin, ps); break; // S0 -> SIGNAL1
    case 1: HAL_GPIO_WritePin(SIG2_GPIO_Port, SIG2_Pin, ps); break; // S1 -> SIGNAL2
    case 2: HAL_GPIO_WritePin(SIG3_GPIO_Port, SIG3_Pin, ps); break; // S2 -> SIGNAL3
    default: break;
  }
}


static void Nextion_ParseByte(uint8_t b)
{
  switch (nx_state)
  {
    case NX_WAIT_S:
      if (b == 'S') nx_state = NX_WAIT_ID;
      break;

    case NX_WAIT_ID:
      if (b >= '0' && b <= '2') {
        nx_switch_id = (uint8_t)(b - '0');
        nx_state = NX_WAIT_EQ;
      } else {
        nx_state = NX_WAIT_S;
      }
      break;

    case NX_WAIT_EQ:
      if (b == '=') {
        nx_val = 0;
        nx_state = NX_VAL0;
      } else {
        nx_state = NX_WAIT_S;
      }
      break;

    case NX_VAL0:
      nx_val |= ((uint32_t)b << 0);
      nx_state = NX_VAL1;
      break;

    case NX_VAL1:
      nx_val |= ((uint32_t)b << 8);
      nx_state = NX_VAL2;
      break;

    case NX_VAL2:
      nx_val |= ((uint32_t)b << 16);
      nx_state = NX_VAL3;
      break;

    case NX_VAL3:
      nx_val |= ((uint32_t)b << 24);

      // Full message received: Sx= + 4 bytes
      ApplySwitch(nx_switch_id, nx_val);

      // Reset to look for next message
      nx_state = NX_WAIT_S;
      break;

    default:
      nx_state = NX_WAIT_S;
      break;
  }
}




uint8_t Cmd_End[3] = { 0xFF, 0xFF, 0xFF };

//Function for sending numerical values to the display
void NEXTION_SendTemp(const char *ID, float value) {

	char buf[50];
	int len = sprintf(buf, "%s.txt=\"%.2fF\"", ID, value); // 2 decimal precision
	HAL_UART_Transmit(&huart1, (uint8_t*) buf, len, 1000);
	HAL_UART_Transmit(&huart1, Cmd_End, 3, 100);
}

void NEXTION_SendPressure(const char *ID, float value) {

	char buf[50];
	int len = sprintf(buf, "%s.txt=\"%.2f\"", ID, value); // 2 decimal precision
	HAL_UART_Transmit(&huart1, (uint8_t*) buf, len, 1000);
	HAL_UART_Transmit(&huart1, Cmd_End, 3, 100);
}

//Function for sending strings to the display
void NEXTION_SendMsg(const char *ID, const char *msg) {

	char buf[50];
	int len = sprintf(buf, "%s.txt=\"%s\"", ID, msg);
	HAL_UART_Transmit(&huart1, (uint8_t*) buf, len, 1000);
	HAL_UART_Transmit(&huart1, Cmd_End, 3, 100);
}

//Calulates the temperature of the cherokee temperture sensor given the voltage across the sensor.
float CalculateTemp(float Vin){

	//Voltage divider reference resistor resistance
	int reference_resistance = 1000;

	float resistance = -1 * reference_resistance * ((Vin) / (Vin - 3.3));

	//Steinhart-hart equation using coefficients derived from FSM. Accurate between 100 and 260 degrees F.
	float temp = 1 / ( 0.001180677409 + 0.0003505018927*log(resistance) - 0.000001315104928*pow(log(resistance), 3) );

	temp = (temp - 273.15) * 9/5 + 32;

	return temp;

};

//Calulates the pressure of the cherokee oil pressure transduce given the voltage across the sensor.
float CalculatePressure(float Vin){

	int reference_resistance = 1000;
	float resistance;
	float pressure;

	//TODO: Fix everything, change formula to include overages on graph
	resistance = -1 * reference_resistance * ((Vin) / (Vin - 3.3));
	pressure = 0.01581472788 * pow(resistance, 2) + 0.006331904355 * resistance - 0.1716084384;

	if(Vin > 0.02){

		return pressure;

	}else{

		return 0;

	}
};
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

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
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_USART1_UART_Init();
  MX_ADC1_Init();
  MX_SPI2_Init();
  MX_FATFS_Init();



  /* USER CODE BEGIN 2 */

		FRESULT log_init = logger_init(&g_logger, &USERFatFS, USERPath, "log.csv");
		(void)log_init;
		// Optional: show result on Nextion if you want
		// char m[32]; sprintf(m,"log_init=%d",(int)log_init); NEXTION_SendMsg("page1.t1", m);

		HAL_ADC_Start_DMA(&hadc1, (uint32_t*)values_adc, 2);
		HAL_UART_Receive_IT(&huart1, &nx_rx_byte, 1);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
float padc = 0;
float padc_sum = 0;
float padc_buf[WINDOW] = {0};
float padc_avg = 0;

float cadc = 0;
float cadc_sum = 0;
float cadc_buf[WINDOW] = {0};
float cadc_avg = 0;

bool init = 0;

int i = 0;

//float cadc_sum = 0;

		while (1) {

			padc = values_adc[1];
			padc_sum += padc;
			padc_sum -= padc_buf[i];
			padc_buf[i] = padc;

			cadc = values_adc[0];
			cadc_sum += cadc;
			cadc_sum -= cadc_buf[i];
			cadc_buf[i] = cadc;

			i++;
			if(i >= WINDOW){
				i = 0;
				init = 1;
			}

			//coolant_voltage = values_adc[0] * (3.3 / 4096);
			if(init == 1){

				padc_avg = padc_sum / WINDOW;
				cadc_avg = cadc_sum / WINDOW;

				pressure_voltage = padc_avg * (3.3 / 4096);
				coolant_voltage = cadc_avg * (3.3 / 4096);
			}else{
				pressure_voltage = values_adc[1] * (3.3 / 4096);
				coolant_voltage = values_adc[0] * (3.3 / 4096);
			}

			/*
			 Moving average algorithm. Adds latest voltage to sum of values in window, removes oldest,
			 then adds latest value to window, or buffer
			 */

			coolant_temp = CalculateTemp(coolant_voltage = values_adc[0] * (3.3 / 4096));
			oil_pressure = CalculatePressure(pressure_voltage = values_adc[1] * (3.3 / 4096));


			//Debug light
			//HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_3);
			// HAL_Delay(300);




			if (coolant_temp > 100 && coolant_temp < 212){

				NEXTION_SendTemp("page1.t0", coolant_temp);
				HAL_Delay(250);

			}else if(coolant_temp < 100){

				NEXTION_SendTemp("page1.t0", coolant_temp);
				HAL_Delay(250);

			}else if(coolant_temp > 212){

				NEXTION_SendMsg("page1.t0", ">260F");
				HAL_Delay(250);

			}else{

				NEXTION_SendMsg("page1.t0", "ERROR");
				HAL_Delay(250);

			}

			//Oil Pressure
			if (oil_pressure >= 0 && oil_pressure < 87){

				NEXTION_SendPressure("page3.t0", oil_pressure);
				HAL_Delay(250);

			}else if(oil_pressure < 0){

				NEXTION_SendMsg("page3.t0", "Imploding");
				HAL_Delay(250);

			}else if(oil_pressure > 87){

				NEXTION_SendMsg("page3.t0", ">87 PSI");
				HAL_Delay(250);

			}else{

				NEXTION_SendMsg("page3.t0", "ERROR");
				HAL_Delay(250);

			}
			/*  functional code

			logger_task(&g_logger,
			            HAL_GetTick(),
			            5000UL,
			            coolant_temp,
			            oil_pressure,
			            -1.0f);   // fuel placeholder for now

			*/
			//test
			logger_task(&g_logger,
			            HAL_GetTick(),
			            0UL,
			            185.5f,     // fake coolant temp
			            42.3f,      // fake oil pressure
			            63.7f);     // fake fuel level

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 2;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 9600;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET); // SD CS idle HIGH

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PB12 PB3 PB4 PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {


    Nextion_ParseByte(nx_rx_byte);

    // Re-arm interrupt to receive next byte
    HAL_UART_Receive_IT(&huart1, &nx_rx_byte, 1);
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
		/* User can add his own implementation to report the HAL error return state */
		__disable_irq();
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
