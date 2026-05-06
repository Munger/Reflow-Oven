#include "api.h"

void ApiSensorsGetTemperature(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_NOT_FOUND;
    }
}

void ApiSensorsGetMains(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_NOT_FOUND;
    }
}
