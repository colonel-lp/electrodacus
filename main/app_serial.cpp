#include "app_serial.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/uart.h"

#include "globals.hpp"

#define UART_RX_BUF 1024
#define UART_TX_BUF 0
#define UART_RES_STRLEN 10
#define UART_RES_NUM_ELEMENTS 14

//queue for uart events (driver -> reader task)
static QueueHandle_t uart_queue;

//queue for result events (reader task -> main loop)
static QueueHandle_t uart_result_queue;


void uartPrintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    size_t size_string = vsnprintf(NULL, 0, fmt, args); //size of the formatted string
    va_end(args);

    char string[size_string + 1]; //leave room for the terminating 0 byte

    va_start(args, fmt);
    vsnprintf(string, size_string + 1, fmt, args);
    va_end(args);

    uart_write_bytes(UART_NUM_0, string, size_string);
}


static void uartTask(void *parameter)
{
    uart_event_t event;
    uint8_t rxBuf[UART_RX_BUF];

    for(;;)
    {
        if(xQueueReceive(uart_queue, (void *)&event, (TickType_t)portMAX_DELAY))
        {
            if(event.type == UART_DATA)
            {
                size_t len = 0;
                ESP_ERROR_CHECK(uart_get_buffered_data_len(UART_NUM_0, &len));

                int readLen = uart_read_bytes(UART_NUM_0, rxBuf, len, 10);

                for(int i = 0; i < readLen; i++)
                {
                    std::string parsed = varStore.handleChar((char)rxBuf[i]);

                    if(!parsed.empty())
                    {
                        char parseEvent[UART_RES_STRLEN];
                        strlcpy(parseEvent, parsed.c_str(), UART_RES_STRLEN);

                        xQueueSendToBack(uart_result_queue, parseEvent, 0); //don't wait if the queue is full
                    }
                }
            }
            else
            {
                varStore.reset();
            }
        }
    }
}

std::string uartPopEvent()
{
    char event[UART_RES_STRLEN];

    if(xQueueReceive(uart_result_queue, event, 0))
    {
        return std::string(event);
    }

    return std::string();
}


void serial_start()
{
    //create queue for result events
    uart_result_queue = xQueueCreate(UART_RES_NUM_ELEMENTS, UART_RES_STRLEN);

    //configure uart and create reading task
    uart_config_t uartConfig = {};
    uartConfig.baud_rate = 921600;
    uartConfig.data_bits = UART_DATA_8_BITS;
    uartConfig.parity    = UART_PARITY_DISABLE;
    uartConfig.stop_bits = UART_STOP_BITS_1;
    uartConfig.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uartConfig.source_clk = UART_SCLK_DEFAULT;

    uart_param_config(UART_NUM_0, &uartConfig);
    uart_driver_install(UART_NUM_0, UART_RX_BUF, UART_TX_BUF, 20, &uart_queue, 0);

    xTaskCreate(uartTask, "uart", 2048 + UART_RX_BUF, NULL, 15, NULL);
}
