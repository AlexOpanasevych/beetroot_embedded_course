# Module 6.2 — FreeRTOS: 4 задачі з різними пріоритетами

STM32CubeIDE проєкт (STM32F411CEUx "BlackPill", FreeRTOS через CMSIS-RTOS2, USART1 @115200 для консолі), що демонструє чотири задачі з чітким розподілом обов'язків і пріоритетів:

- **Sensor Reader** (Normal) — імітує опитування датчика кожні 500 мс *точно*: використовує `osDelayUntil`, прив'язаний до попереднього моменту пробудження, а не звичайний `osDelay`, щоб час виконання задачі не накопичував дрейф періоду.
- **Event Trigger** (Low) — раз на 3 с "прокидається" (`osDelay`) і динамічно створює `Worker Task` через `osThreadNew`.
- **Worker Task** (High, створюється лише під час події) — виводить повідомлення й одразу коректно завершує себе викликом `osThreadExit()` (аналог `vTaskDelete(NULL)` у CMSIS-RTOS2) — вихід через `return`/`}` без видалення заборонений.
- **Maintenance Mode** (Realtime) — раз на 10 с ставить `Sensor Reader` на паузу (`osThreadSuspend`), 2 секунди тримає паузу, потім відновлює (`osThreadResume`).

Код задач: `Core/Src/main.c`.

## Збірка

Відкрити `STM32CubeIDE/` як проєкт у STM32CubeIDE (Import → Existing Projects) і зібрати звичайним чином, або скрипт-збірку через `arm-none-eabi-gcc` напряму (без CubeIDE) — усі джерела вже лежать у `Core/`, `Drivers/`, `Middlewares/`, лінкер-скрипт `STM32CubeIDE/STM32F411CEUX_FLASH.ld`.

HAL-таймбейс перенесено на TIM11 (`Core/Src/stm32f4xx_hal_timebase_tim.c`), бо SysTick повністю зайнятий тіком планувальника FreeRTOS після старту `osKernelStart()`.
