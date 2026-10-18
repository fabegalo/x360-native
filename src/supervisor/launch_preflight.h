#pragma once
#include "catalog.h"

namespace x360n {
// Speculative read-ahead only. VerifyCatalogLaunch and the session gates still
// authenticate every required identity; a missing/invalid file grants nothing.
void PrimeLaunchVerification(const ProjectPaths& paths,
    const std::vector<desktop::Target>& catalog, const std::string& selected);
}
