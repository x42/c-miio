#include "json.h"

#ifdef __cplusplus
extern "C" {
#endif

int miio_init ();
void miio_cleanup ();
json_value* miio_cmd (const char* cmd, const char* opt);
const char* vac_status (int code);
const char* vac_error (int code);

#ifdef __cplusplus
}
#endif
