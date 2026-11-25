// libseamhub — stub. Task 1 pins the ABI; later tasks fill in the bodies.
#include "seam_hub.h"

struct SeamHub { int unused; };

namespace { SeamHub g_hub; }

extern "C" {
uint32_t seam_hub_v1_version(void) { return SEAM_HUB_VERSION; }
SeamHub* seam_hub_v1_get(void) { return &g_hub; }
int32_t  seam_hub_v1_register(SeamHub*, const char*, uint32_t, uint32_t) { return SEAM_HUB_NO_HANDLE; }
void     seam_hub_v1_unregister(SeamHub*, int32_t) {}
void     seam_hub_v1_publish_state(SeamHub*, int32_t, const SeamHubState*) {}
int32_t  seam_hub_v1_members(SeamHub*, SeamHubMember*, int32_t) { return 0; }
uint64_t seam_hub_v1_post(SeamHub*, int32_t, SeamHubProposal*) { return 0; }
int32_t  seam_hub_v1_proposals(SeamHub*, uint64_t, SeamHubProposal*, int32_t) { return 0; }
int32_t  seam_hub_v1_lifecycle(SeamHub*, int32_t, uint64_t) { return kSeamHubNone; }
int32_t  seam_hub_v1_transition(SeamHub*, int32_t, uint64_t, int32_t, int32_t) { return 0; }
void     seam_hub_v1_sheet_status(SeamHub*, SeamHubSheetStatus*) {}
int32_t  seam_hub_v1_sheet_set_folder(SeamHub*, const char*) { return -1; }
void     seam_hub_v1_sheet_new(SeamHub*) {}
}
