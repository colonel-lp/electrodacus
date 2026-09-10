#pragma once

#include <cstddef>
#include <cstdint>

// Minimal SMTP client for the once-a-day status report.
//
// email_send_status() dispatches the mail on its own short-lived FreeRTOS task
// (a full TLS handshake plus the SMTP round-trips take several seconds and must
// not stall the main loop). It returns true if the send was *dispatched*, false
// if a send is already in flight or the worker task could not be created.
// Delivery success/failure is not reported back to the caller.
//
// Configuration is taken from g_settings.em_* (see app_settings.hpp), which is
// loaded from /spiffs/cfg/email.
bool email_send_status(uint8_t soc_percent, int16_t internal_temp_tenth_c);

// Copies a human-readable description of the most recent send attempt into out
// (e.g. "OK - accepted ..." or "FAIL at [AUTH (password)]: 535-5.7.8 ..."), so
// the outcome can be inspected over HTTP even though the serial log is disabled.
void email_last_status(char *out, size_t n);
