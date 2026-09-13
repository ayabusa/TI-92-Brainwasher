#include "main.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "debug.h"
#include "filetransfer.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

SPI_HandleTypeDef hspi1;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
void Write_Address(uint32_t adr);
void Write_Command_Pins(int CE, int OE, int WE);
void Chip_Erase(void);
void Chip_Program_Byte(int addr, uint16_t data);
uint16_t  Chip_Read_Byte(int addr);
void Write_Command(int addr, uint16_t data);
void Data_Pins_Init(int as_output);
uint16_t Receive_Data(void);
void Write_Data(uint16_t value);
void debug_print(const char *s);
char *debug_get_str(void);
char debug_get_char(void);
void Dump_Flash_CDC();
void USB_ForceReenum(void);

uint16_t ROM_DATA_PINS[] = {
    GPIO_PIN_8,
    GPIO_PIN_9,
    GPIO_PIN_10,
    GPIO_PIN_15,
    GPIO_PIN_3,
    GPIO_PIN_4,
    GPIO_PIN_6,
    GPIO_PIN_7,
    GPIO_PIN_8,
    GPIO_PIN_9,
    GPIO_PIN_10,
    GPIO_PIN_11,
    GPIO_PIN_12,
    GPIO_PIN_13,
    GPIO_PIN_14,
    GPIO_PIN_15,
};

GPIO_TypeDef *ROM_DATA_PORT[] = {
    GPIOA,
    GPIOA,
    GPIOA,
    GPIOA,
    GPIOB,
    GPIOB,
    GPIOB,
    GPIOB,
    GPIOB,
    GPIOB,
    GPIOB,
    GPIOB,
    GPIOB,
    GPIOB,
    GPIOB,
    GPIOB,
};

extern USBD_HandleTypeDef hUsbDeviceFS;

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_SPI1_Init();
    USB_ForceReenum();
    MX_USB_DEVICE_Init();
    while (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) { }


    while (1) {
        debug_reset_buf(); // Just in case
        // First we wait for a command packet (forever)
        uint16_t pkt_type;
        if (receive_packet(SIZE_HEADER + SIZE_FOOTER, &pkt_type, NULL, 0)) {
            continue;
        }
        switch (pkt_type) {
            case TYPE_TEST: // This is just the ping packet
                break;
            case TYPE_ERASE:
                Chip_Erase();
                break;
            case TYPE_SEND: { // The PC sends a ROM
                for (uint32_t i = 0; i<PACKET_NUMBER; i++) {
                    uint32_t address;
                    uint8_t  data[PACKET_DATA_SIZE];
                    if (receive_data_packet(&address, data))
                        break;
                    for (uint32_t k=0; k<PACKET_DATA_SIZE/2; k++) {
                        Chip_Program_Byte(address + k, ((uint16_t *)data)[k]);
                        __NOP(); __NOP(); __NOP(); // This is to prevent writing stuff too fast
                    }
                }
                break;
            }
            case TYPE_RECEIVE: { // The PC wants to dump da ROM
                for (uint32_t i = 0; i<PACKET_NUMBER; i++) {
                    uint32_t address = (PACKET_DATA_SIZE/2) * i;
                    uint8_t  data[PACKET_DATA_SIZE] = {0};
                    // We retreive the data
                    for (uint32_t k = 0; k<PACKET_DATA_SIZE/2; k++)
                        ((uint16_t *)data)[k] = Chip_Read_Byte(address + k);
                    // We sent everything
                    send_packet(create_data_packet(address, data),
                                SIZE_HEADER + SIZE_ED_DATA +SIZE_FOOTER,
                                1);
                }
                break;
            }
        }
    }
}

void Write_Address(uint32_t adr) {
    uint8_t bytes[] = {
        (adr & 0xFF0000) >> 16,
        (adr & 0x00FF00) >> 8,
        (adr & 0x0000FF),
    };
    HAL_SPI_Transmit(&hspi1, bytes, 3, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(ADDR_RCLK_PORT, ADDR_RCLK_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ADDR_RCLK_PORT, ADDR_RCLK_PIN, GPIO_PIN_RESET);
}

void Write_Command_Pins(int CE, int OE, int WE) {
    HAL_GPIO_WritePin(CP_PORT, CP_CE_PIN, (CE) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(CP_PORT, CP_OE_PIN, (OE) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(CP_PORT, CP_WE_PIN, (WE) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Chip_Erase(void) {
    // Erase sequence
    Write_Command(0x5555, 0xAAAA);
    Write_Command(0x2AAA, 0x5555);
    Write_Command(0x5555, 0x8080);
    Write_Command(0x5555, 0xAAAA);
    Write_Command(0x2AAA, 0x5555);
    Write_Command(0x5555, 0x1010);

    HAL_Delay(150); // it's 100ms max but by precaution
}

void Chip_Program_Byte(int addr, uint16_t data) {
    Write_Command(0x5555, 0xAAAA);
    Write_Command(0x2AAA, 0x5555);
    Write_Command(0x5555, 0xA0A0);
    Write_Command(addr, data);
}

uint16_t Chip_Read_Byte(int addr) {
    Write_Address(addr);
    Data_Pins_Init(0);
    Write_Command_Pins(0, 0, 1);
    int data = Receive_Data();
    Write_Command_Pins(1, 1, 1);
    return data;
}

void Write_Command(int addr, uint16_t data) {
    Write_Command_Pins(1, 1, 1);
    Write_Address(addr);
    Write_Data(data);
    Write_Command_Pins(0, 1, 1);
    // 4. Pulse WE# low to latch data
    Write_Command_Pins(0, 1, 0);  // WE low
    Write_Command_Pins(0, 1, 1);  // WE high

    // 5. Deassert CE#
    Write_Command_Pins(1, 1, 1);
}

uint16_t Receive_Data(void) {
    Data_Pins_Init(0); // We make sure it's in input mode
    int result = 0;
    for(size_t i=0; i<16; i++){
        if(HAL_GPIO_ReadPin(ROM_DATA_PORT[i], ROM_DATA_PINS[i]) == GPIO_PIN_SET){
            result += 1 << i;
        }
    }
    return __builtin_bswap16(result);
}

void Write_Data(uint16_t value) {
    Data_Pins_Init(1); // We make sure it's in output mode
    value = __builtin_bswap16(value);
    for(size_t i=0; i<16; i++){
        if((value >> i) & 1) HAL_GPIO_WritePin(ROM_DATA_PORT[i], ROM_DATA_PINS[i], GPIO_PIN_SET);
        else HAL_GPIO_WritePin(ROM_DATA_PORT[i], ROM_DATA_PINS[i], GPIO_PIN_RESET);
    }
}

void Dump_Flash_CDC(){
    uint16_t byte;
    char buf[8];

    for (int addr = 0; addr < 0x40; addr++) { // 1 MiB
        byte = Chip_Read_Byte(addr);
        // Send as str
        sprintf(buf, "%02X %02X  ", (byte & 0xFF00)>>8, byte & 0xFF);
        debug_print(buf);
        if ((addr & 0xF) == 0x7) {
            char newline[] = "\r\n";
            debug_print(newline);
        }
    }
}

// The argument must be 0 (input) or 1 (output)
void Data_Pins_Init(int as_output) {
    GPIO_InitTypeDef ga = {0};
    GPIO_InitTypeDef gb = {0};

    // Configure data pins as push-pull outputs
    ga.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10| GPIO_PIN_15;
    gb.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_6 | GPIO_PIN_7 |
        GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10|GPIO_PIN_11 |
        GPIO_PIN_12|GPIO_PIN_13| GPIO_PIN_14| GPIO_PIN_15;
    if(as_output == 1){
        ga.Mode = GPIO_MODE_OUTPUT_PP;   // Push-pull output
        ga.Pull = GPIO_NOPULL;           // No pull-up/down
        ga.Speed = GPIO_SPEED_FREQ_HIGH; // Fast switching

        gb.Mode = GPIO_MODE_OUTPUT_PP;   // Push-pull output
        gb.Pull = GPIO_NOPULL;           // No pull-up/down
        gb.Speed = GPIO_SPEED_FREQ_HIGH; // Fast switching
    }else{
        ga.Mode = GPIO_MODE_INPUT;  // Input mode
        ga.Pull = GPIO_PULLDOWN;      // No pull-up/down

        gb.Mode = GPIO_MODE_INPUT;  // Input mode
        gb.Pull = GPIO_PULLDOWN;      // No pull-up/down
    }
    HAL_GPIO_Init(GPIOA, &ga);
    HAL_GPIO_Init(GPIOB, &gb);
}

void USB_ForceReenum(void) {
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin   = GPIO_PIN_12;          // D+
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_12);
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    /** Initializes the RCC Oscillators according to the specified parameters
        * in the RCC_OscInitTypeDef structure.
        */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
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
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_SPI1_Init(void)
{
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    GPIO_InitTypeDef ga = {0};
    ga.Pin   = ADDR_SRCLK_PIN | ADDR_SER_PIN;
    ga.Mode  = GPIO_MODE_AF_PP;
    ga.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ADDR_SRCLK_PORT, &ga);

    GPIO_InitTypeDef gb = {0};
    gb.Pin   = ADDR_RCLK_PIN;
    gb.Mode  = GPIO_MODE_OUTPUT_PP;
    gb.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ADDR_RCLK_PORT, &gb);

    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_1LINE;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    if (HAL_SPI_Init(&hspi1) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    /*Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);

    /*Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, GPIO_PIN_RESET);

    /*Configure GPIO pin : USER_KEY_Pin */
    GPIO_InitStruct.Pin = USER_KEY_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(USER_KEY_GPIO_Port, &GPIO_InitStruct);

    /*Configure GPIO pin : LED_BLUE_Pin */
    GPIO_InitStruct.Pin = LED_BLUE_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_BLUE_GPIO_Port, &GPIO_InitStruct);

    /*Configure GPIO pin : CE, WE, n OE */
    GPIO_InitStruct.Pin = CP_CE_PIN | CP_OE_PIN | CP_WE_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CP_PORT, &GPIO_InitStruct);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
        HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, 1);
        HAL_Delay(100);
        HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, 0);
        HAL_Delay(100);
    }
}
