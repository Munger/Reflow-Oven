#ifndef USBPD_TASK_H
#define USBPD_TASK_H

#include "taskutils.h"

extern osThreadId_t USBPDTaskHandle;

void USBPDTaskInit( void );
void USBPDTaskLoop( void );

#endif // USBPD_TASK_H