#pragma once

#include <string>

// UART0 link to the SBMS main board. Installs the driver, starts the reader task
// that feeds varStore, and exposes a queue of parsed variable names.
void serial_start();

// Pop one parsed-variable event (e.g. "sbms", "s2"). Empty string if none pending.
std::string uartPopEvent();

// printf() straight to UART0 (bypasses the — disabled — logging subsystem).
void uartPrintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
