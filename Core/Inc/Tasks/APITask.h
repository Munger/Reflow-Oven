#ifndef API_TASK_H
#define API_TASK_H

#include "taskutils.h"

// Public API Task Interface

extern osThreadId_t APITaskHandle;

// Initialises the API core and buffer streams
void APITaskInit( void );

// Main execution logic for the API Task loop
void APITaskLoop( void );

// USB Transmission Complete Callback for usbd_cdc_if.c
void USBTxDoneHandler( void );

#endif // API_TASK_H