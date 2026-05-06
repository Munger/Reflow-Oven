#include "api.h"

void ApiOvenGetStatus(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_NOT_FOUND;
    }
}

void ApiOvenPutRun(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_CONFLICT;
    }
}

void ApiOvenPutStop(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_OK;
    }
}

void ApiOvenPutEstop(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_OK;
    }
}

void ApiOvenPutManualEnable(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_CONFLICT;
    }
}

void ApiOvenPutManualDisable(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_OK;
    }
}

void ApiOvenPutManualHeater(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_BAD_REQUEST;
    }
}

void ApiOvenPutManualFan(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_BAD_REQUEST;
    }
}
