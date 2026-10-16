# x360-native: original Retail 2.0.17559.0 avatar service for any title whose
# manifest declares avatar_service = "2.0.17559.0" (docs/avatar-system.md §17).
# The code runs in the session dashboard's single XAM image, bound by its system
# UI host and shared with the Guide; the title embeds no XAM of its own for it.
set(X360N_AVATAR_SERVICE_17559_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(x360n_add_avatar_service_17559 target)
    set(dir "${X360N_AVATAR_SERVICE_17559_DIR}")
    target_sources(${target} PRIVATE
        ${dir}/avatar_service_17559.cpp
        ${dir}/avatar_toc.cpp
        ${dir}/avatar_metadata.cpp
        ${dir}/avatar_assets_request.cpp
        ${dir}/avatar_aot_assets.cpp
        ${dir}/avatar_aot_catalog.cpp)
endfunction()
