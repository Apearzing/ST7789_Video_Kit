/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  *          Demo 1: orientation test (which side is TOP / BOTTOM).
  *          Demo 2: bouncing ball with simple physics (gravity + restitution),
  *                  running forever. Hardware SPI1 (PA5=SCK, PA7=MOSI).
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
#include "spi.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "st7789_hspi.h"
#include "usart.h"
#include "video_player.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* (bouncing-ball demo removed: video-only firmware) */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* (ball physics defines removed: video-only firmware) */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void DrawArrowUp(uint16_t cx, uint16_t tipY, uint16_t size, uint16_t color);
static void DrawArrowDown(uint16_t cx, uint16_t tipY, uint16_t size, uint16_t color);
static void OrientationTest(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* White arrow pointing UP: triangle + shaft. tipY = row of the tip. */
static void DrawArrowUp(uint16_t cx, uint16_t tipY, uint16_t size, uint16_t color)
{
    for (uint16_t i = 0; i <= size; i++)
    {
        uint16_t y = tipY + i;
        uint16_t xa = (cx > i) ? (cx - i) : 0;
        uint16_t xb = cx + i;
        if (xb > 239) xb = 239;
        ST7789_DrawFillRect(xa, y, xb, y, color);
    }
    ST7789_DrawFillRect(cx - size / 4, tipY + size + 1,
                        cx + size / 4, tipY + size * 2, color);
}

/* White arrow pointing DOWN: triangle + shaft. tipY = row of the tip. */
static void DrawArrowDown(uint16_t cx, uint16_t tipY, uint16_t size, uint16_t color)
{
    for (uint16_t i = 0; i <= size; i++)
    {
        uint16_t y = tipY - i;
        uint16_t xa = (cx > i) ? (cx - i) : 0;
        uint16_t xb = cx + i;
        if (xb > 239) xb = 239;
        ST7789_DrawFillRect(xa, y, xb, y, color);
    }
    ST7789_DrawFillRect(cx - size / 4, tipY - size * 2,
                        cx + size / 4, tipY - size - 1, color);
}

/* Orientation test card, shown for ~4 seconds:
 *   TOP edge    = RED bar    + white UP arrow
 *   BOTTOM edge = BLUE bar   + white DOWN arrow
 *   LEFT edge   = GREEN bar, RIGHT edge = YELLOW bar
 * If the RED bar + UP arrow are physically on top, MADCTL (0x36) is correct.
 * If upside down, change ST7789_Init: 0x36 data 0x00 -> 0xC0. */
static void OrientationTest(void)
{
    ST7789_Clear(BLACK);
    ST7789_DrawFillRect(0, 0, 239, 15, RED);      // top marker
    ST7789_DrawFillRect(0, 224, 239, 239, BLUE);   // bottom marker
    ST7789_DrawFillRect(0, 0, 15, 239, GREEN);     // left marker
    ST7789_DrawFillRect(224, 0, 239, 239, YELLOW); // right marker
    DrawArrowUp(120, 40, 22, WHITE);               // "this way is UP"
    DrawArrowDown(120, 199, 22, WHITE);            // "this way is DOWN"
    ST7789_DrawFillCircle(120, 120, 14, WHITE);    // center reference
    HAL_Delay(4000);
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
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  ST7789_Init();
  OrientationTest();  // 4-second direction card
  ST7789_Clear(BLACK); // card done -> black screen while waiting
  /* Video-only firmware: wait for the PC stream forever, then play.
   * If the PC stops, the last frame stays and we keep waiting. */
  Video_WaitForStream(0xFFFFFFFFUL);
  Video_Loop();       // never returns
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 144;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

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
  *         where the assert_param error has occurred.
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