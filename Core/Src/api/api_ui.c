#include "api.h"

void ApiUIPutLight(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_OK;
    }
}

void ApiUIPutBuzzer(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_OK;
    }
}
