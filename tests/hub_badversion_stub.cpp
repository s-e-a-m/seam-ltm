// A stub that answers the version query with a version the client must refuse.
// It exports the full v1 symbol set, so a client that reaches ANY of these has
// skipped the version gate, which is exactly the bug this test catches.
#include "seam_hub.h"

static SeamHub* g_fake = (SeamHub*)1;

extern "C" {
SEAM_HUB_API uint32_t seam_hub_v1_version(void) { return 2u; }
SEAM_HUB_API SeamHub* seam_hub_v1_get(void) { return g_fake; }
SEAM_HUB_API int32_t seam_hub_v1_register(SeamHub*, const char*, uint32_t, uint32_t, uint32_t) { return 0; }
SEAM_HUB_API void seam_hub_v1_unregister(SeamHub*, int32_t) {}
SEAM_HUB_API void seam_hub_v1_publish_state(SeamHub*, int32_t, const SeamHubState*) {}
SEAM_HUB_API int32_t seam_hub_v1_members(SeamHub*, SeamHubMember*, int32_t) { return 7; }
SEAM_HUB_API uint64_t seam_hub_v1_post(SeamHub*, int32_t, SeamHubProposal*) { return 7; }
SEAM_HUB_API int32_t seam_hub_v1_proposals(SeamHub*, uint64_t, SeamHubProposal*, int32_t) { return 7; }
SEAM_HUB_API int32_t seam_hub_v1_lifecycle(SeamHub*, int32_t, uint64_t) { return 7; }
SEAM_HUB_API int32_t seam_hub_v1_lifecycle_of(SeamHub*, uint32_t, uint32_t, uint64_t) { return 7; }
SEAM_HUB_API int32_t seam_hub_v1_transition(SeamHub*, int32_t, uint64_t, int32_t, int32_t, const char*) { return 7; }
SEAM_HUB_API int32_t seam_hub_v1_recent_lines(SeamHub*, char*, int32_t) { return 7; }
SEAM_HUB_API void seam_hub_v1_sheet_status(SeamHub*, SeamHubSheetStatus*) {}
SEAM_HUB_API int32_t seam_hub_v1_sheet_set_folder(SeamHub*, const char*) { return 0; }
SEAM_HUB_API void seam_hub_v1_sheet_new(SeamHub*) {}
}
