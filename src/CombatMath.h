#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
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
			const float L = (std::max)(1.0f, level);
			float inc = 0.0f;

			const auto addBand = [&](float lo, float hi, float slope) {
				if (L <= lo) {
					return;
				}
				const float upper = (std::min)(L, hi);
				const float n = (std::max)(0.0f, upper - lo);
				inc += n * slope;
			};

			addBand(1.0f, 71.0f, 0.40f);
			addBand(71.0f, 91.0f, 1.00f);
			addBand(91.0f, 160.0f, 0.21f);
			addBand(160.0f, 713.0f, 0.036f);
			return inc;
		}

		// Layer 1: one stat-derived defense for all physical subtypes; one value per elemental type
		// (Magic, Fire, Frost, Poison, Lightning — same order as Esp::DamageTypeId 4..8).
		constexpr std::size_t kReqElementalL1Count = 5;

		struct Req_DefenseBucketsL1
		{
			float physical = 0.0f;
			std::array<float, kReqElementalL1Count> elemental{};
		};

		// Pure math: no engine or ERAS includes here. Used when ERAS is **unavailable**: callers pass
		// level, maxHP, maxMP, armorRating from the actor (e.g. kHealth / kMagicka / kDamageResist).
		// When `TryGetActorSnapshot` succeeds, strict override uses `PlayerStatsSnapshot::defense`
		// (DefenseSheet) per damage type instead of this function — see OverrideDamageHook.
		[[nodiscard]] inline Req_DefenseBucketsL1 Req_ComputeDefenseBucketsL1(
			float level,
			float maxHP,
			float maxMP,
			float armorRating)
		{
			const float base = Req_BaseDefense(level) + Req_LevelIncrement(level);

			// Physical extras: +0.1 * (health/10) + 0.1 * armorRating
			const float healthTerm = 0.1f * ((std::max)(0.0f, maxHP) / 10.0f);
			const float armorTerm = 0.1f * (std::max)(0.0f, armorRating);

			// Elemental base (per type): base + level increment + magicka term
			const float magickaTerm = 0.1f * ((std::max)(0.0f, maxMP) / 10.0f);
			const float elementalCore = base + magickaTerm;

			Req_DefenseBucketsL1 out{};
			out.physical = base + healthTerm + armorTerm;
			out.elemental.fill(elementalCore);
			return out;
		}

		[[nodiscard]] inline float Req_ApplyLayer1Clamp(float raw, float defense)
		{
			if (raw <= 0.0f) {
				return 0.0f;
			}
			const float rawSafe = (std::max)(0.0f, raw);
			const float unclamped = rawSafe - (std::max)(0.0f, defense);
			const float lo = 0.1f * rawSafe;
			const float hi = 0.9f * rawSafe;
			return (std::max)(lo, (std::min)(hi, unclamped));
		}

		// ---------------------------
		// Weapon attribute scaling (Elden-style, weapon_attribute_scaling.md)
		// ---------------------------
		// Bonus per stat line: floor(max(0, baseDamage) * scalingMultiplier * saturation(statLevel)).
		// scalingMultiplier: decimal (e.g. 1.60). Esp ingestion divides CK MGEF magnitude (percent) by 100.

		struct WeaponAttrScalingCoeffs
		{
			float str = 0.0f;
			float dex = 0.0f;
			float intl = 0.0f;
			float fth = 0.0f;
			float arc = 0.0f;
		};

		// Piecewise linear saturation curve (stat 1..99) approximating the soft-cap notes in
		// weapon_attribute_scaling.md: ~20% by low 20s, ~75% by mid 50s, ~90% at 80, ~100% at 99.
		[[nodiscard]] inline float WeaponAttrScalingSaturation(std::int32_t a_statLevel)
		{
			int s = static_cast<int>(a_statLevel);
			if (s < 1) {
				s = 1;
			} else if (s > 99) {
				s = 99;
			}
			const float x = static_cast<float>(s);
			const auto seg = [](float xa, float ya, float xb, float yb, float x) -> float {
				if (x <= xa) {
					return ya;
				}
				if (x >= xb) {
					return yb;
				}
				const float t = (x - xa) / (xb - xa);
				return ya + t * (yb - ya);
			};
			if (x <= 20.0f) {
				return seg(1.0f, 0.0f, 20.0f, 0.20f, x);
			}
			if (x <= 55.0f) {
				return seg(20.0f, 0.20f, 55.0f, 0.75f, x);
			}
			if (x <= 80.0f) {
				return seg(55.0f, 0.75f, 80.0f, 0.90f, x);
			}
			return seg(80.0f, 0.90f, 99.0f, 1.0f, x);
		}

		[[nodiscard]] inline float WeaponAttrScalingBonusDamage(
			float a_baseDamage,
			const WeaponAttrScalingCoeffs& a_weapon,
			std::int32_t a_str,
			std::int32_t a_dex,
			std::int32_t a_intl,
			std::int32_t a_fth,
			std::int32_t a_arc)
		{
			const double base = static_cast<double>((std::max)(0.0f, a_baseDamage));
			if (base <= 0.0) {
				return 0.0f;
			}

			const auto term = [&](float mag, std::int32_t lv) -> double {
				if (!(mag > 0.0f)) {
					return 0.0;
				}
				const double sat = static_cast<double>(WeaponAttrScalingSaturation(lv));
				return std::floor(base * static_cast<double>(mag) * sat);
			};

			double sum = 0.0;
			sum += term(a_weapon.str, a_str);
			sum += term(a_weapon.dex, a_dex);
			sum += term(a_weapon.intl, a_intl);
			sum += term(a_weapon.fth, a_fth);
			sum += term(a_weapon.arc, a_arc);

			if (!std::isfinite(sum) || sum <= 0.0) {
				return 0.0f;
			}
			if (sum >= static_cast<double>((std::numeric_limits<float>::max)())) {
				return (std::numeric_limits<float>::max)();
			}
			return static_cast<float>(sum);
		}

		// Legacy §3 (pre–tier-premium model). Prefer SpellStaffMissileMultiplier for new balance.
		[[nodiscard]] inline float SpellStaffSorceryScalingMultiplier(
			float a_catalystBase,
			float a_scaleCoef,
			float a_statSaturation)
		{
			return a_catalystBase + a_catalystBase * a_scaleCoef * a_statSaturation;
		}

		// spell_staff_damage.md §4 — bare-hands multiplier (multiply VanillaMag).
		[[nodiscard]] inline float SpellBareHandsScalingMultiplier(float a_innateCoef, float a_statSaturation)
		{
			return 1.0f + a_innateCoef * a_statSaturation;
		}

		// spell_staff_damage.md §3 (balanced) — staff uses same baseline as hands, then a small tier premium and
		// optional staff-enchant INT/FTH term. a_tier01 = 0..1 from catalyst score in [min,max].
		[[nodiscard]] inline float SpellStaffMissileMultiplier(
			float a_handLikeMult,
			float a_tier01,
			float a_premiumMin,
			float a_premiumMax,
			float a_scaleCoef,
			float a_statSaturation,
			float a_enchantScaleK)
		{
			const float t = (std::clamp)(a_tier01, 0.0f, 1.0f);
			const float prem = a_premiumMin + t * (a_premiumMax - a_premiumMin);
			const float sc = (std::max)(0.0f, a_scaleCoef);
			const float sat = (std::max)(0.0f, a_statSaturation);
			const float k = (std::max)(0.0f, a_enchantScaleK);
			const float enchant = 1.0f + k * sc * sat;
			const float hand = (std::max)(0.0f, a_handLikeMult);
			return hand * prem * enchant;
		}

		// ---------------------------
		// Block stamina (requirement.md §5)
		// ---------------------------
		// Incoming = BaseStaminaDamage * MotionMult; received = max(0, incoming - guardPoint).

		[[nodiscard]] inline float BlockStaminaBaseFromWeaponWeight(float a_weaponWeight)
		{
			const float w = (std::max)(0.0f, a_weaponWeight);
			return 10.0f + w * 1.2f;
		}

		[[nodiscard]] inline float BlockStaminaMotionMultiplier(bool a_powerAttack, bool a_sprinting)
		{
			if (a_sprinting && a_powerAttack) {
				return 2.0f;
			}
			if (a_sprinting || a_powerAttack) {
				return 1.5f;
			}
			return 1.0f;
		}

		[[nodiscard]] inline float BlockStaminaIncoming(float a_aggressorWeaponWeight, bool a_powerAttack, bool a_sprinting)
		{
			const float base = BlockStaminaBaseFromWeaponWeight(a_aggressorWeaponWeight);
			const float mult = BlockStaminaMotionMultiplier(a_powerAttack, a_sprinting);
			return (std::max)(0.0f, base * mult);
		}

		// Guard point is already resolved (shield armor rating or defender §5.1 base from weapon weight).
		[[nodiscard]] inline float BlockStaminaReceived(float a_incoming, float a_guardPoint)
		{
			const float incoming = (std::max)(0.0f, a_incoming);
			const float guard = (std::max)(0.0f, a_guardPoint);
			return (std::max)(0.0f, incoming - guard);
		}

		[[nodiscard]] inline float Req_ApplyLayer2Mitigation(float postL1, const std::vector<float>& mitigationPercents)
		{
			if (postL1 <= 0.0f) {
				return 0.0f;
			}
			double prod = 1.0;
			for (float n : mitigationPercents) {
				// Accept minor out-of-range values gracefully.
				const double nd = static_cast<double>(n);
				const double clamped = (std::max)(0.0, (std::min)(1.0, nd));
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
			const float damageFloor = (std::max)(atk * cfg.damage_floor_fraction, cfg.damage_floor_min);
			dmg = (std::max)(dmg, damageFloor);
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
			const float denom = 1.0f + k_resist * (std::max)(resValue, 0.0f);
			if (denom <= 0.0f) return payload;
			return payload / denom;
		}

		[[nodiscard]] inline float AccumulateMeter(float meter, float effectivePayload)
		{
			return (std::min)(100.0f, meter + effectivePayload);
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
			const double dtAfterDelay = (std::max)(0.0, timeSince - decayDelaySeconds);
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
			for (float w : weights) sum += (std::max)(0.0f, w);
			if (sum <= 0.0f) return EqualSplitWeights(weights.size());

			std::vector<float> out = weights;
			for (float& w : out) w = (std::max)(0.0f, w) / sum;
			return out;
		}
	}
}

