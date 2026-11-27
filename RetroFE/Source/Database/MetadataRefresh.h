#pragma once
#include <filesystem>

namespace MetadataRefresh
{
    inline bool needed(bool locked, int count, std::filesystem::file_time_type db,
                       std::filesystem::file_time_type exe, std::filesystem::file_time_type meta,
                       std::filesystem::file_time_type overrides)
    {
        return db < overrides || (!locked && (count == 0 || db < meta || db < exe));
    }
}
