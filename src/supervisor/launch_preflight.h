#pragma once
#include "catalog.h"
#include "session_profile.h"

namespace x360n {
// Speculative read-ahead only. VerifyCatalogLaunch and the session gates still
// authenticate every required identity; a missing/invalid file grants nothing.
void PrimeLaunchVerification(const ProjectPaths& paths,
    const std::vector<desktop::Target>& catalog, const std::string& selected);
// A compatibility recipe supplies routing metadata, not library membership.
void BindLibraryLaunchTargets(SessionProfile& profile,
    const std::vector<desktop::Target>& catalog);
}
