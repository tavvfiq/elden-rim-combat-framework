#pragma once

#include <array>
#include <vector>

#include <RE/A/Actor.h>
#include <RE/E/EffectSetting.h>

namespace ERCF
{
	namespace Esp
	{
		enum class DamageTypeId : std::uint32_t
		{
			Standard = 0,
			Strike = 1,
			Slash = 2,
			Pierce = 3,
			Magic = 4,
			Fire = 5,
			Lightning = 6,
			Holy = 7,
			kTotal
		};

		constexpr std::size_t kDamageTypeCount = static_cast<std::size_t>(DamageTypeId::kTotal);

		struct MitigationCoefficients
		{
			std::array<float, kDamageTypeCount> defense{};
			// Multiplicative layer-2 absorption sources, stored as absorbed fractions.
			std::array<std::vector<float>, kDamageTypeCount> absorptionFractions{};
		};

		struct StatusResistanceCoefficients
		{
			float immunityResValue = 0.0f;
			float robustnessResValue = 0.0f;
		};

		struct StatusBuildupCoefficients
		{
			float poisonPayload = 0.0f;
			float bleedPayload = 0.0f;
		};

		// Scans actor active effects and extracts ERCF.MGEF-* contributions.
		// Notes:
		// - v1 uses active effects as the delivery mechanism for Defense/Absorption/ResBand/Buildup MGEFs.
		// - Later we can refine to read from weapon/spell effect settings rather than actor-wide effects.
		void ExtractMitigationFromActiveEffects(const RE::Actor* a_target, MitigationCoefficients& a_out);
		void ExtractStatusResistancesFromActiveEffects(const RE::Actor* a_target, StatusResistanceCoefficients& a_out);
		void ExtractStatusPayloadFromActiveEffects(const RE::Actor* a_attacker, StatusBuildupCoefficients& a_out);
	}
}

