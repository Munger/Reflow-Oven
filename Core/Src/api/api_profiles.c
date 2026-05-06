#include "api.h"

void ApiProfilesGetList(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_NOT_FOUND;
    }
}

void ApiProfilesGet(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_NOT_FOUND;
    }
}

void ApiProfilesPost(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_CREATED;
    }
}

void ApiProfilesDelete(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_NO_CONTENT;
    }
}
