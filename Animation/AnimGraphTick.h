#pragma once

namespace Dark
{
	class World;
	class AssetManager;

	// Skip when dt == 0. Does not write prevWorld (G-buffer owns that).
	void tickAnimGraphs(World& world, AssetManager& assets, float dt);
}
