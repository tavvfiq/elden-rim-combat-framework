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
		// ---------------------------
		// Requirement (v2) HP model
		// ---------------------------
		//
		// Layer 1: flat defense with clamp per component
		//   postL1 = clamp(raw - defense, 0.1*raw, 0.9*raw)
		//
		// Layer 2: multiplicative mitigation per component bucket
		//   postL2 = postL1 * Π(1 - N_i)   where N_i are percentages in [0,1]
		//
		// Notes:
		// - These helpers are additive alongside existing v1 math until strict override is fully wired.

		[[nodiscard]] inline float Req_BaseDefense(float level)
		{
			// BaseDefense = 10 + (level + 78) / 2.483
			return 10.0f + (level + 78.0f) / 2.483f;
		}

		[[nodiscard]] inline float Req_LevelIncrement(float level)
		{
			// Piecewise increment (additive with base defense):
			// - Level 1..71: +0.40 per level
			// - Level 72..91: +1.00 per level
			// - Level 92..160: +0.21 per level
			// - Level 161..713: +0.036 per level
			//
			// Treat level <= 1 as 0 increment.
			const float L = std::max(1.0f, level);
			float inc = 0.0f;

			const auto addBand = [&](float lo, float hi, float slope) {
				if (L <= lo) {
					return;
				}
				const float upper = std::min(L, hi);
				const float n = std::max(0.0f, upper - lo);
				inc += n * slope;
			};

			addBand(1.0f, 71.0f, 0.40f);
			addBand(71.0f, 91.0f, 1.00f);
			addBand(91.0f, 160.0f, 0.21f);
			addBand(160.0f, 713.0f, 0.036f);
			return inc;
		}

		struct Req_DefenseBucketsL1
		{
			float physical = 0.0f;
			// Elemental buckets: Fire/Frost/Poison/Lightning/Magic all share the same base+level+magicka term;
			// type-specific L2 mitigation differentiates them later.
			float elemental = 0.0f;
		};

		[[nodiscard]] inline Req_DefenseBucketsL1 Req_ComputeDefenseBucketsL1(
			float level,
			float maxHP,
			float maxMP,
			float armorRating)
		{
			const float base = Req_BaseDefense(level) + Req_LevelIncrement(level);

			// Physical extras: +0.1 * (health/10) + 0.1 * armorRating
			const float healthTerm = 0.1f * (std::max(0.0f, maxHP) / 10.0f);
			const float armorTerm = 0.1f * std::max(0.0f, armorRating);

			// Elemental extras: +0.1 * (magicka/10)
			const float magickaTerm = 0.1f * (std::max(0.0f, maxMP) / 10.0f);

			Req_DefenseBucketsL1 out{};
			out.physical = base + healthTerm + armorTerm;
			out.elemental = base + magickaTerm;
			return out;
		}

		[[nodiscard]] inline float Req_ApplyLayer1Clamp(float raw, float defense)
		{
			if (raw <= 0.0f) {
				return 0.0f;
			}
			const float rawSafe = std::max(0.0f, raw);
			const float unclamped = rawSafe - std::max(0.0f, defense);
			const float lo = 0.1f * rawSafe;
			const float hi = 0.9f * rawSafe;
			return std::clamp(unclamped, lo, hi);
		}

		[[nodiscard]] inline float Req_ApplyLayer2Mitigation(float postL1, const std::vector<float>& mitigationPercents)
		{
			if (postL1 <= 0.0f) {
				return 0.0f;
			}
			double prod = 1.0;
			for (float n : mitigationPercents) {
				// Accept minor out-of-range values gracefully.
				const double clamped = std::clamp(static_cast<double>(n), 0.0, 1.0);
				prod *= (1.0 - clamped);
			}
			return static_cast<float>(postL1 * prod);
		}

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

		[[nodiscard]] inline float ApplyTakenDamageMultiplier(float damage, float takenMult)
		{
			return damage * takenMult;
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

		// Universal decay (linear):
		// - meter holds for DecayDelay seconds after last buildup
		// - then meter drains: meter -= decayRatePointsPerSecond * dt
		[[nodiscard]] inline float DecayMeter(
			float meter,
			double dt,
			double timeSinceLastBuildup,
			double decayDelaySeconds,
			double decayRatePointsPerSecond)
		{
			if (meter <= 0.0f) return 0.0f;
			if (dt <= 0.0) return meter;
			if (timeSinceLastBuildup < decayDelaySeconds) return meter;

			const double next = static_cast<double>(meter) - decayRatePointsPerSecond * dt;
			return static_cast<float>((std::max)(0.0, next));
		}

		// Value of a buildup meter at wall time `evalSec` given the last committed value at `lastBuildupWallSec`
		// (same model as DecayMeter / ApplyDecayAndAccumulate).
		[[nodiscard]] inline float MeterValueAtWallTime(
			float meterAtLastCommit,
			double lastBuildupWallSec,
			double evalWallSec,
			double decayDelaySeconds,
			double decayRate,
			bool decayEnabled = true)
		{
			if (meterAtLastCommit <= 0.0f) {
				return 0.0f;
			}
			if (!decayEnabled) {
				return meterAtLastCommit;
			}
			if (lastBuildupWallSec <= 0.0 || evalWallSec <= lastBuildupWallSec) {
				return meterAtLastCommit;
			}
			const double timeSince = evalWallSec - lastBuildupWallSec;
			const double dtAfterDelay = std::max(0.0, timeSince - decayDelaySeconds);
			return DecayMeter(
				meterAtLastCommit,
				dtAfterDelay,
				timeSince,
				decayDelaySeconds,
				decayRate);
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

