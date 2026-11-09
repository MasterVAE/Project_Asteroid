#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

enum RESOURCE_TYPE
{
    IRON,
    COPPER,
    SILICON,
    WATER,
    OXYGEN
};

struct Resource_data
{
    std::string name;
    std::string description;
    float mass;
};

static const std::unordered_map<RESOURCE_TYPE, Resource_data> RESOURCE_DATA = {
    {IRON,      {"Iron",    "A common metal used in construction and manufacturing.",           7.87f}},
    {COPPER,    {"Copper",  "A reddish-brown metal used in electrical wiring and plumbing.",    8.96f}},
    {SILICON,   {"Silicon", "A metalloid used in electronics and solar panels.",                2.33f}},
    {WATER,     {"Water",   "A vital resource for life and various industrial processes.",      1.0f}},
    {OXYGEN,    {"Oxygen",  "A gas essential for respiration and combustion.",                  0.001429f}}
};

static const std::size_t RESOURCE_COUNT = RESOURCE_DATA.size();
