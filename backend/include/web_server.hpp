#pragma once

class World;

// Blocks while serving HTTP + WebSocket. Configuration: ASTEROID_* environment variables.
void RunServer(World& world);
