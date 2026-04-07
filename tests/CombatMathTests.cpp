#include "../src/CombatMath.h"

#include <cassert>

namespace
{
	void TestDefenseCurve()
	{
		ERCF::Config::Values cfg{};

		// If defense=0, PostDefense should return atk.
		{
			float atk = 10.0f;
			float out = ERCF::Math::PostDefense(atk, 0.0f, cfg.k_defense);
			assert(std::abs(out - atk) < 0.0001f);
		}

		// Defense should reduce damage vs no-defense.
		{
			float atk = 10.0f;
			float out0 = ERCF::Math::PostDefense(atk, 0.0f, cfg.k_defense);
			float out1 = ERCF::Math::PostDefense(atk, 10.0f, cfg.k_defense);
			assert(out1 < out0);
		}
	}

	void TestMeterMath()
	{
		{
			float m = 90.0f;
			m = ERCF::Math::AccumulateMeter(m, 15.0f);
			assert(m == 100.0f);
			assert(ERCF::Math::MeterPops(m));
		}

		// Decay hold phase.
		{
			float m = 50.0f;
			double dt = 0.1;
			float out = ERCF::Math::DecayMeter(m, dt, /*timeSinceLastBuildup*/ 0.1, /*decayDelay*/ 0.75, /*rate pts/s*/ 5.0);
			assert(std::abs(out - m) < 0.0001f);
		}

		// Linear drain after hold: 5 points per second for 1s in decay window -> 50 -> 45.
		{
			float m = 50.0f;
			float out = ERCF::Math::DecayMeter(m, 1.0, /*timeSinceLastBuildup*/ 1.0, /*decayDelay*/ 0.75, 5.0);
			assert(std::abs(out - 45.0f) < 0.0001f);
		}
	}

	void TestAbsorptionMath()
	{
		ERCF::Config::Values cfg{};
		cfg.damage_floor_fraction = 0.001f;
		cfg.damage_floor_min = 0.0f;

		// With no absorption, DamageAfterDefenseAndAbsorption should be >= PostDefense.
		{
			float atk = 10.0f;
			float defense = 5.0f;
			std::vector<float> abs{};
			const float out = ERCF::Math::DamageAfterDefenseAndAbsorption(atk, defense, abs, cfg);
			assert(out > 0.0f);
		}

		// More absorption sources should reduce absorbed fraction product.
		{
			float atk = 10.0f;
			float defense = 5.0f;
			std::vector<float> abs1{ 0.1f };
			std::vector<float> abs2{ 0.1f, 0.1f };
			const float d1 = ERCF::Math::DamageAfterDefenseAndAbsorption(atk, defense, abs1, cfg);
			const float d2 = ERCF::Math::DamageAfterDefenseAndAbsorption(atk, defense, abs2, cfg);
			assert(d2 <= d1);
		}
	}

	void TestWeaponAttrScaling()
	{
		using ERCF::Math::WeaponAttrScalingBonusDamage;
		using ERCF::Math::WeaponAttrScalingCoeffs;
		using ERCF::Math::WeaponAttrScalingSaturation;

		assert(std::abs(WeaponAttrScalingSaturation(1)) < 0.001f);
		assert(std::abs(WeaponAttrScalingSaturation(20) - 0.20f) < 0.02f);
		assert(WeaponAttrScalingSaturation(55) >= 0.74f && WeaponAttrScalingSaturation(55) <= 0.76f);
		assert(WeaponAttrScalingSaturation(80) >= 0.89f && WeaponAttrScalingSaturation(80) <= 0.91f);
		assert(std::abs(WeaponAttrScalingSaturation(99) - 1.0f) < 0.001f);

		WeaponAttrScalingCoeffs c{};
		c.str = 1.60f;
		const float bonus = WeaponAttrScalingBonusDamage(245.0f, c, 80, 10, 10, 10, 10);
		assert(bonus >= 350.0f && bonus <= 354.0f);
	}

	void TestSpellStaffMissileMultiplier()
	{
		using ERCF::Math::SpellBareHandsScalingMultiplier;
		using ERCF::Math::SpellStaffMissileMultiplier;
		const float hand = SpellBareHandsScalingMultiplier(3.0f, 0.9f);
		const float staffMid = SpellStaffMissileMultiplier(hand, 0.5f, 1.05f, 1.15f, 0.9f, 0.9f, 0.25f);
		assert(staffMid > hand);
		const float staffLow = SpellStaffMissileMultiplier(hand, 0.0f, 1.05f, 1.15f, 0.0f, 0.9f, 0.25f);
		const float staffHigh = SpellStaffMissileMultiplier(hand, 1.0f, 1.05f, 1.15f, 0.0f, 0.9f, 0.25f);
		assert(staffHigh >= staffLow);
	}

	void TestBlockStamina()
	{
		using ERCF::Math::BlockStaminaBaseFromWeaponWeight;
		using ERCF::Math::BlockStaminaIncoming;
		using ERCF::Math::BlockStaminaMotionMultiplier;
		using ERCF::Math::BlockStaminaReceived;

		assert(std::abs(BlockStaminaBaseFromWeaponWeight(15.0f) - 28.0f) < 0.001f);
		assert(std::abs(BlockStaminaMotionMultiplier(false, false) - 1.0f) < 0.001f);
		assert(std::abs(BlockStaminaMotionMultiplier(true, false) - 1.5f) < 0.001f);
		assert(std::abs(BlockStaminaMotionMultiplier(false, true) - 1.5f) < 0.001f);
		assert(std::abs(BlockStaminaMotionMultiplier(true, true) - 2.0f) < 0.001f);
		assert(std::abs(BlockStaminaIncoming(15.0f, false, false) - 28.0f) < 0.001f);
		assert(std::abs(BlockStaminaReceived(28.0f, 10.0f) - 18.0f) < 0.001f);
		assert(std::abs(BlockStaminaReceived(5.0f, 10.0f) - 0.0f) < 0.001f);
	}

	void TestSplitMath()
	{
		{
			auto w = ERCF::Math::EqualSplitWeights(2);
			assert(w.size() == 2);
			assert(std::abs(w[0] - 0.5f) < 0.0001f);
			assert(std::abs(w[1] - 0.5f) < 0.0001f);
		}
		{
			std::vector<float> w{ 2.0f, 6.0f };
			auto out = ERCF::Math::NormalizeSplitWeights(w);
			assert(out.size() == 2);
			assert(std::abs((out[0] + out[1]) - 1.0f) < 0.0001f);
		}
	}
}

// Intentionally not invoked automatically.
// This is a reference harness for a future xmake "tests" target on Windows.
void RunCombatMathTests()
{
	TestDefenseCurve();
	TestMeterMath();
	TestAbsorptionMath();
	TestWeaponAttrScaling();
	TestSpellStaffMissileMultiplier();
	TestBlockStamina();
	TestSplitMath();
}

