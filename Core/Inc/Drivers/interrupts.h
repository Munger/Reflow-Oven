#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include "stdbool.h"

void EnableDriverInterrupts( void );
void DisableriverInterrupts( void );
bool DriverInterruptsEnabled( void );

#endif // INTERRUPTS_H