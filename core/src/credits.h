#pragma once
#include <module.h>

namespace sdrpp_credits {
    SDRPP_EXPORT const char* contributors[];
    SDRPP_EXPORT const char* libraries[];
    SDRPP_EXPORT const char* hardwareDonators[];
    SDRPP_EXPORT const char* patrons[];
    SDRPP_EXPORT const int contributorCount;
    SDRPP_EXPORT const int libraryCount;
    SDRPP_EXPORT const int hardwareDonatorCount;
    SDRPP_EXPORT const int patronCount;
}

namespace sdr4p_credits {
    SDRPP_EXPORT const char* sdr4pLibraries[];
    SDRPP_EXPORT const int sdr4pLibraryCount;
}