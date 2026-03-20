#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

#include "Config.h"

namespace ERCF
{
	namespace Math
	{
		// v1: PostDefense_T = ATK_T^2 / (ATK_T + k_defense * Defense_T)
		[[nodiscard]] inline float PostDefense(float atk, float defense, float k_defense)
		{
			if (atk <= 0.0f) return 0.0f;
			if (defense <= 0.0f) return atk;

			const float denom = atk + k_defense * defense;
			if (denom <= 0.0f) return atk;
			return (atk * atk) / denom;
		}

		// v1: AbsProd_T = Π_i (1 - abs_i,T)
		[[nodiscard]] inline float AbsorptionProduct(const std::vector<float>& absFractions)
		{
			float prod = 1.0f;
			for (float abs : absFractions) {
				prod *= (1.0f - abs);
			}
			return prod;
		}

		// v1: Damage_T = PostDefense_T * AbsProd_T, then clamped to a floor.
		[[nodiscard]] inline float DamageAfterDefenseAndAbsorption(
			float atk,
			float defense,
			const std::vector<float>& absFractions,
			const Config::Values& cfg)
		{
			const float postDefense = PostDefense(atk, defense, cfg.k_defense);
			const float absProd = AbsorptionProduct(absFractions);
			float dmg = postDefense * absProd;

			// Contract floor prevents hard 0 "forever" in normal gameplay.
			const float damageFloor = std::max(atk * cfg.damage_floor_fraction, cfg.damage_floor_min);
			dmg = std::max(dmg, damageFloor);
			return dmg;
		}

		// v1: EffectivePayload = Payload / (1 + k_resist * ResValue_band)
		[[nodiscard]] inline float EffectivePayload(float payload, float resValue, float k_resist)
		{
			if (payload <= 0.0f) return 0.0f;
			const float denom = 1.0f + k_resist * std::max(resValue, 0.0f);
			if (denom <= 0.0f) return payload;
			return payload / denom;
		}

		[[nodiscard]] inline float AccumulateMeter(float meter, float effectivePayload)
		{
			return std::min(100.0f, meter + effectivePayload);
		}

		[[nodiscard]] inline bool MeterPops(float meter)
		{
			return meter >= 100.0f;
		}

		// v1 decay:
		// - meter holds for DecayDelay seconds after last buildup
		// - then meter decays: meter *= exp(-decay_rate * dt)
		[[nodiscard]] inline float DecayMeter(
			float meter,
			double dt,
			double timeSinceLastBuildup,
			double decayDelaySeconds,
			double decayRate)
		{
			if (meter <= 0.0f) return 0.0f;
			if (dt <= 0.0) return meter;
			if (timeSinceLastBuildup < decayDelaySeconds) return meter;

			const double decay = std::exp(-decayRate * dt);
			return static_cast<float>(meter * decay);
		}

		// Split helper:
		// v1 contract is flexible; for now we support:
		// - equal split
		// - normalize arbitrary weights
		[[nodiscard]] inline std::vector<float> EqualSplitWeights(std::size_t componentCount)
		{
			if (componentCount == 0) return {};
			return std::vector<float>(componentCount, 1.0f / static_cast<float>(componentCount));
		}

		[[nodiscard]] inline std::vector<float> NormalizeSplitWeights(const std::vector<float>& weights)
		{
			if (weights.empty()) return {};
			float sum = 0.0f;
			for (float w : weights) sum += std::max(0.0f, w);
			if (sum <= 0.0f) return EqualSplitWeights(weights.size());

			std::vector<float> out = weights;
			for (float& w : out) w = std::max(0.0f, w) / sum;
			return out;
		}
	}
}

