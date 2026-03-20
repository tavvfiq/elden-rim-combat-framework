#pragma once

namespace ERCF
{
	namespace Prisma
	{
		// Initializes PrismaUI view and meter hooks. Safe to call on kDataLoaded.
		void Init();

		// Updates Poison/Bleed meters for the player.
		void UpdateMeters(float poisonMeter, float bleedMeter);
	}
}

