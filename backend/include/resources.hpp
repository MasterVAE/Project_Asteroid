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
    CERAMIC,
    SILICON_WAFER,
    COPPER_WIRE,
    GOLD_WIRE,
    CARBON_FIBER,
    GRAPHENE,
    DIAMOND,
    SILICON_CARBIDE,
    POLYMER,
    LUBRICANT,
    ROCKET_FUEL,

    // Structural and mechanical parts.
    IRON_PLATE,
    STEEL_PLATE,
    COMPOSITE_PLATE,
    GEAR,
    PIPE,
    TANK,
    HULL_SECTION,

    // Electronics and power components.
    CIRCUIT_BOARD,
    PROCESSOR,
    SENSOR,
    OPTICAL_LENS,
    SOLAR_CELL,
    BATTERY_CELL,
    CAPACITOR,
    ELECTRIC_MOTOR,

    // Finished equipment and supplies (inventory items).
    SOLAR_ARRAY,
    BATTERY_PACK,
    RADIATOR,
    DRILL_HEAD,
    THRUSTER_UNIT,
    NAVIGATION_COMPUTER,
    REPAIR_KIT,
    OXYGEN_CANISTER,
    FUEL_CANISTER,
    MINING_DRONE,
    SURVEY_PROBE,
    FACTORY_CORE
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
    {CERAMIC,             "ceramic",             1.875f},
    {SILICON_WAFER,       "silicon_wafer",       0.5f},
    {COPPER_WIRE,         "copper_wire",         0.25f},
    {GOLD_WIRE,           "gold_wire",           0.25f},
    {CARBON_FIBER,        "carbon_fiber",        0.5f},
    {GRAPHENE,            "graphene",            0.25f},
    {DIAMOND,             "diamond",             2.0f},
    {SILICON_CARBIDE,     "silicon_carbide",     2.0f},
    {POLYMER,             "polymer",             1.125f},
    {LUBRICANT,           "lubricant",           1.25f},
    {ROCKET_FUEL,         "rocket_fuel",         1.0f},

    {IRON_PLATE,          "iron_plate",          2.0f},
    {STEEL_PLATE,         "steel_plate",         3.0f},
    {COMPOSITE_PLATE,     "composite_plate",     4.0f},
    {GEAR,                "gear",                1.0f},
    {PIPE,                "pipe",                1.0f},
    {TANK,                "tank",                5.0f},
    {HULL_SECTION,        "hull_section",        10.0f},

    {CIRCUIT_BOARD,       "circuit_board",       2.125f},
    {PROCESSOR,           "processor",           1.0f},
    {SENSOR,              "sensor",              2.875f},
    {OPTICAL_LENS,        "optical_lens",        2.75f},
    {SOLAR_CELL,          "solar_cell",          1.5f},
    {BATTERY_CELL,        "battery_cell",        2.125f},
    {CAPACITOR,           "capacitor",           1.875f},
    {ELECTRIC_MOTOR,      "electric_motor",      4.0f},

    {SOLAR_ARRAY,         "solar_array",         6.5f},
    {BATTERY_PACK,        "battery_pack",        8.375f},
    {RADIATOR,            "radiator",            4.875f},
    {DRILL_HEAD,          "drill_head",          7.0f},
    {THRUSTER_UNIT,       "thruster_unit",       8.0f},
    {NAVIGATION_COMPUTER, "navigation_computer", 8.875f},
    {REPAIR_KIT,          "repair_kit",          4.625f},
    {OXYGEN_CANISTER,     "oxygen_canister",     6.75f},
    {FUEL_CANISTER,       "fuel_canister",       7.0f},
    {MINING_DRONE,        "mining_drone",        38.25f},
    {SURVEY_PROBE,        "survey_probe",        36.125f},
    {FACTORY_CORE,        "factory_core",        31.25f}
};

// Only this prefix of RESOURCES can occur naturally in asteroids.
static const std::size_t RAW_RESOURCES_COUNT = static_cast<std::size_t>(GOLD) + 1;
static const std::size_t RESOURCES_COUNT = sizeof(RESOURCES)/sizeof(RESOURCES[0]);


struct RECIPIE
{
    // Each entry represents one unit: {IRON, IRON, CARBON} means 2 iron + 1 carbon.
    std::vector<RESOURCE_TYPE> input;
    std::vector<RESOURCE_TYPE> output;
};

inline const RECIPIE RECIPIES[]
{
    // Refining: ice -> water -> gases -> fuel and industrial materials.
    {{ICE},                                      {WATER}},
    {{WATER},                                    {HYDROGEN, OXYGEN}},
    {{IRON, IRON, CARBON},                        {STEEL}},
    {{SILICON, OXYGEN, OXYGEN},                   {GLASS}},
    {{SILICON, OXYGEN},                           {CERAMIC}},
    {{SILICON},                                  {SILICON_WAFER, SILICON_WAFER}},
    {{COPPER},                                   {COPPER_WIRE, COPPER_WIRE, COPPER_WIRE, COPPER_WIRE}},
    {{GOLD},                                     {GOLD_WIRE, GOLD_WIRE, GOLD_WIRE, GOLD_WIRE}},
    {{CARBON},                                   {CARBON_FIBER, CARBON_FIBER}},
    {{CARBON},                                   {GRAPHENE, GRAPHENE, GRAPHENE, GRAPHENE}},
    {{CARBON, CARBON},                           {DIAMOND}},
    {{SILICON, CARBON},                          {SILICON_CARBIDE}},
    {{CARBON, HYDROGEN},                         {POLYMER}},
    {{POLYMER, HYDROGEN},                        {LUBRICANT}},
    {{HYDROGEN, OXYGEN},                         {ROCKET_FUEL}},

    // Structural parts: basic metalworking -> reinforced hull sections.
    {{IRON, IRON},                              {IRON_PLATE}},
    {{STEEL},                                   {STEEL_PLATE}},
    {{STEEL_PLATE, CARBON_FIBER, CARBON_FIBER},   {COMPOSITE_PLATE}},
    {{IRON},                                    {GEAR}},
    {{IRON},                                    {PIPE}},
    {{IRON_PLATE, IRON_PLATE, PIPE},             {TANK}},
    {{COMPOSITE_PLATE, COMPOSITE_PLATE, IRON_PLATE}, {HULL_SECTION}},

    // Electronics: wafers and conductors -> control and power components.
    {{SILICON_WAFER, COPPER_WIRE, COPPER_WIRE, POLYMER}, {CIRCUIT_BOARD}},
    {{SILICON_WAFER, GOLD_WIRE, GRAPHENE},        {PROCESSOR}},
    {{CIRCUIT_BOARD, SILICON_WAFER, GOLD_WIRE},   {SENSOR}},
    {{GLASS},                                   {OPTICAL_LENS}},
    {{SILICON_WAFER, SILICON_WAFER, COPPER_WIRE, GOLD_WIRE}, {SOLAR_CELL}},
    {{COPPER, CARBON, HYDROGEN},                 {BATTERY_CELL}},
    {{COPPER_WIRE, COPPER_WIRE, GRAPHENE, POLYMER}, {CAPACITOR}},
    {{IRON_PLATE, GEAR, COPPER_WIRE, COPPER_WIRE, COPPER_WIRE, COPPER_WIRE},
                                                 {ELECTRIC_MOTOR}},

    // Inventory items only; these recipes do not install ship modules.
    {{SOLAR_CELL, SOLAR_CELL, GLASS, COPPER_WIRE, COPPER_WIRE, COPPER_WIRE},
                                                 {SOLAR_ARRAY}},
    {{BATTERY_CELL, BATTERY_CELL, BATTERY_CELL, IRON_PLATE}, {BATTERY_PACK}},
    {{COPPER, COPPER, PIPE, CERAMIC},             {RADIATOR}},
    {{STEEL_PLATE, DIAMOND, SILICON_CARBIDE},     {DRILL_HEAD}},
    {{STEEL_PLATE, CERAMIC, PIPE, CIRCUIT_BOARD}, {THRUSTER_UNIT}},
    {{PROCESSOR, PROCESSOR, SENSOR, CIRCUIT_BOARD, CAPACITOR}, {NAVIGATION_COMPUTER}},
    {{IRON_PLATE, CARBON_FIBER, CARBON_FIBER, COPPER_WIRE, COPPER_WIRE, POLYMER},
                                                 {REPAIR_KIT}},
    {{TANK, OXYGEN, OXYGEN},                     {OXYGEN_CANISTER}},
    {{TANK, ROCKET_FUEL, ROCKET_FUEL},            {FUEL_CANISTER}},
    {{HULL_SECTION, DRILL_HEAD, ELECTRIC_MOTOR, NAVIGATION_COMPUTER, BATTERY_PACK},
                                                 {MINING_DRONE}},
    {{HULL_SECTION, THRUSTER_UNIT, NAVIGATION_COMPUTER, OPTICAL_LENS, SOLAR_ARRAY},
                                                 {SURVEY_PROBE}},
    {{HULL_SECTION, ELECTRIC_MOTOR, ELECTRIC_MOTOR, PROCESSOR, CIRCUIT_BOARD,
      DRILL_HEAD, LUBRICANT, CAPACITOR},          {FACTORY_CORE}},

    // Recovery recipes return exactly the original quantities, without duplication.
    {{WATER},                                   {ICE}},
    {{SILICON_WAFER, SILICON_WAFER},              {SILICON}},
    {{COPPER_WIRE, COPPER_WIRE, COPPER_WIRE, COPPER_WIRE}, {COPPER}},
    {{GOLD_WIRE, GOLD_WIRE, GOLD_WIRE, GOLD_WIRE}, {GOLD}},
    {{CARBON_FIBER, CARBON_FIBER},                {CARBON}},
    {{GRAPHENE, GRAPHENE, GRAPHENE, GRAPHENE},    {CARBON}},
    {{IRON_PLATE},                              {IRON, IRON}},
    {{STEEL_PLATE},                             {STEEL}},
    {{OXYGEN_CANISTER},                         {TANK, OXYGEN, OXYGEN}},
    {{FUEL_CANISTER},                           {TANK, ROCKET_FUEL, ROCKET_FUEL}}
};

static const std::size_t RECIPIES_COUNT = sizeof(RECIPIES)/sizeof(RECIPIES[0]);

