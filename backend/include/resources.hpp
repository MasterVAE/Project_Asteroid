#pragma once

#include <string>

enum RESOURCE_TYPE
{
    SILICON,
    CARBON,
    IRON,
    ICE,
    COPPER,
    GOLD
};

struct RESOURCE_DATA
{
    RESOURCE_TYPE type;
    std::string name;
};

inline const RESOURCE_DATA RESOURCES[] =
{
    {SILICON, "silicon"},
    {CARBON, "carbon"},
    {IRON, "iron"},
    {ICE, "ice"},
    {COPPER, "copper"},
    {GOLD, "gold"}
};

static const std::size_t RESOURCES_COUNT = sizeof(RESOURCES)/sizeof(RESOURCES[0]);
