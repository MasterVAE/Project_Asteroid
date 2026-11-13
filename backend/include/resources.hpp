#pragma once

#include <string>

enum RESOURCE_TYPE
{
    ROCK,
    STONE
};

struct RESOURCE_DATA
{
    RESOURCE_TYPE type;
    std::string name;
};

inline const RESOURCE_DATA RESOURCES[] =
{
    {ROCK, "rock"},
    {STONE, "stone"}
};

static const std::size_t RESOURCES_COUNT = sizeof(RESOURCES)/sizeof(RESOURCES[0]);
