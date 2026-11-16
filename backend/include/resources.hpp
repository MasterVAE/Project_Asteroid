#pragma once

#include <cstddef>
#include <string>
#include <vector>

enum RESOURCE_TYPE
{
    SILICON,
    CARBON,
    IRON,
    ICE,
    COPPER,
    GOLD,

    // Refined materials and chemicals.
    WATER,
    HYDROGEN,
    OXYGEN,
    STEEL,
    GLASS,
    ROCKET_FUEL
};

struct RESOURCE_DATA
{
    RESOURCE_TYPE type;
    std::string name;
    float mass;
};

inline const RESOURCE_DATA RESOURCES[] =
{
    // Keep mineable resources first, in their original order.
    {SILICON,   "silicon",  1},
    {CARBON,    "carbon",   1},
    {IRON,      "iron",     1},
    {ICE,       "ice",      1},
    {COPPER,    "copper",   1},
    {GOLD,      "gold",     1},

    // Masses are game units; each recipe conserves total item mass.
    {WATER,               "water",               1.0f},
    {HYDROGEN,            "hydrogen",            0.125f},
    {OXYGEN,              "oxygen",              0.875f},
    {STEEL,               "steel",               3.0f},
    {GLASS,               "glass",               2.75f},
    {ROCKET_FUEL,         "rocket_fuel",           1.0f}
};
// Only this prefix of RESOURCES can occur naturally in asteroids.
static const std::size_t RAW_RESOURCES_COUNT = static_cast<std::size_t>(GOLD) + 1;
static const std::size_t RESOURCES_COUNT = sizeof(RESOURCES)/sizeof(RESOURCES[0]);


struct RECIPE
{
    // Each entry represents one unit: {IRON, IRON, CARBON} means 2 iron + 1 carbon.
    std::vector<RESOURCE_TYPE> input;
    std::vector<RESOURCE_TYPE> output;
};

inline const RECIPE RECIPES[]
{
    // Refining: ice -> water -> gases -> fuel and industrial materials.
    {{ICE},                                      {WATER}},
    {{WATER},                                    {HYDROGEN, OXYGEN}},
    {{IRON, IRON, CARBON},                        {STEEL}},
    {{SILICON, OXYGEN, OXYGEN},                   {GLASS}},
    {{HYDROGEN, OXYGEN},                         {ROCKET_FUEL}},

};

static const std::size_t RECIPES_COUNT = sizeof(RECIPES)/sizeof(RECIPES[0]);

