#ifndef ROTARYENCODER_H
#define ROTARYENCODER_H

#include "types.h"

// Status of the magnetic link between the sensor and the magnet.
typedef enum {
    MagStatusOk = 0,
    MagStatusTooLow,    // Magnet is too far away
    MagStatusTooHigh,   // Magnet is too close
    MagStatusNotFound   // No magnet detected
} MagStatus;

// Opaque handle to an encoder instance.
typedef struct RotaryEncoder* RotaryEncoderRef;

// Initialise the encoder module.
void      REInitModule( void );

// Open a handle to the encoder. 
// Uses the I2C bus configured in CubeMX.
RotaryEncoderRef REOpen( void );

// Returns the current rotational speed.
Rpm       REGetRpm( RotaryEncoderRef Encoder );

// Returns the raw angle in permille of a full rotation (0-1000).
Permille  REGetAngle( RotaryEncoderRef Encoder );

// Returns the diagnostic status of the magnetic field.
MagStatus REGetMagStatus( RotaryEncoderRef Encoder );

#endif // ROTARYENCODER_H