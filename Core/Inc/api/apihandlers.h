#ifndef APIHANDLERS_H
#define APIHANDLERS_H

#include "apitypes.h"


typedef PayloadPtr ( *APIHandler )( APIPBPtr pb );

// Oven
PayloadPtr HandlerOvenStatus( APIPBPtr pb );
PayloadPtr HandlerOvenRun( APIPBPtr pb );
PayloadPtr HandlerOvenStop( APIPBPtr pb );
PayloadPtr HandlerOvenEstop( APIPBPtr pb );

// Manual Control
PayloadPtr HandlerManualEnable( APIPBPtr pb );
PayloadPtr HandlerManualDisable( APIPBPtr pb );
PayloadPtr HandlerManualHeater( APIPBPtr pb );
PayloadPtr HandlerManualFan( APIPBPtr pb );

// Sensors
PayloadPtr HandlerSensorsTemp( APIPBPtr pb );
PayloadPtr HandlerSensorsMains( APIPBPtr pb );

// Profiles
PayloadPtr HandlerProfilesList( APIPBPtr pb );
PayloadPtr HandlerProfileGet( APIPBPtr pb );
PayloadPtr HandlerProfileCreate( APIPBPtr pb );
PayloadPtr HandlerProfileUpdate( APIPBPtr pb );
PayloadPtr HandlerProfileDelete( APIPBPtr pb );

// Config
PayloadPtr HandlerConfigGet( APIPBPtr pb );
PayloadPtr HandlerConfigPut( APIPBPtr pb );

// Logs
PayloadPtr HandlerLogsList( APIPBPtr pb );
PayloadPtr HandlerLogGet( APIPBPtr pb );
PayloadPtr HandlerLogDelete( APIPBPtr pb );
PayloadPtr HandlerLogsClear( APIPBPtr pb );

// Storage
PayloadPtr HandlerStorageGet( APIPBPtr pb );
PayloadPtr HandlerStorageFormat( APIPBPtr pb );

// System
PayloadPtr HandlerSystemStatus( APIPBPtr pb );
PayloadPtr HandlerClockGet( APIPBPtr pb );
PayloadPtr HandlerClockPut( APIPBPtr pb );
PayloadPtr HandlerSystemReset( APIPBPtr pb );

// Power
PayloadPtr HandlerPowerGet( APIPBPtr pb );

// UI
PayloadPtr HandlerUiLight( APIPBPtr pb );
PayloadPtr HandlerUiBuzzer( APIPBPtr pb );

#endif // APIHANDLERS_H