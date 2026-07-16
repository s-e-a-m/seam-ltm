// A stub that answers the version query with a version the client must refuse.
// It exports the full v1 symbol set, so a client that reaches ANY of these has
// skipped the version gate — which is exactly the bug this test catches.
#include "seam_calbus.h"

static SeamCalbus* g_fake = (SeamCalbus*)1;

extern "C" {
SEAM_CALBUS_API uint32_t seam_calbus_v1_version(void) { return 99u; }
SEAM_CALBUS_API SeamCalbus* seam_calbus_v1_get(void) { return g_fake; }
SEAM_CALBUS_API int32_t seam_calbus_v1_register(SeamCalbus*) { return 0; }
SEAM_CALBUS_API void seam_calbus_v1_unregister(SeamCalbus*, int32_t) {}
SEAM_CALBUS_API void seam_calbus_v1_publish(SeamCalbus*, int32_t, const SeamCalbusRecord*) {}
SEAM_CALBUS_API int32_t seam_calbus_v1_snapshot(SeamCalbus*, SeamCalbusRecord*, int32_t) { return 7; }
}
