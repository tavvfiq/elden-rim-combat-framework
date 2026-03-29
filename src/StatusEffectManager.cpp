#include "pch.h"

#include "StatusEffectManager.h"

#include "Config.h"
#include "ERLSIntegration.h"
#include "EspRouting.h"
#include "Log.h"
#include "Messaging.h"
#include "PrismaUIMeters.h"

#include "RE/A/Actor.h"
#include "RE/M/MainMenu.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESObjectWEAP.h"
#include "RE/U/UI.h"
#include "SKSE/Interfaces.h"

#ifdef GetObject
#	undef GetObject
#endif

#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ERCF
{
	namespace StatusEffects
	{
		namespace
		{
			struct TargetState
			{
				float poisonMeter = 0.0f;
				float bleedMeter = 0.0f;
				float rotMeter = 0.0f;
				float frostbiteMeter = 0.0f;
				float sleepMeter = 0.0f;
				float madnessMeter = 0.0f;

				double poisonLastBuildupSec = 0.0;
				double bleedLastBuildupSec = 0.0;
				double rotLastBuildupSec = 0.0;
				double frostbiteLastBuildupSec = 0.0;
				double sleepLastBuildupSec = 0.0;
				double madnessLastBuildupSec = 0.0;

				std::uint32_t immunityProcCount = 0;
				std::uint32_t robustnessProcCount = 0;
				std::uint32_t focusProcCount = 0;
				std::uint32_t madnessProcCount = 0;
			};

			std::mutex g_mutex;
			std::unordered_map<std::uint32_t, TargetState> g_targets;

			std::deque<StatusProcMessage> g_pendingPops;
			std::atomic<bool> g_flushQueued{ false };

			std::atomic<bool> g_playerBuildupHudDirty{ false };

			[[nodiscard]] double NowSeconds();

			// --- Poison / rot DoT (ticked from Actor::Update, throttled by per-dot nextTickSec) ---
			struct PoisonDotState
			{
				double nextTickSec = 0.0;
				std::uint32_t ticksRemaining = 0;
				float tickDamage = 0.0f;
			};

			std::unordered_map<std::uint32_t, PoisonDotState> s_poisonDots;
			std::unordered_map<std::uint32_t, PoisonDotState> s_rotDots;
			std::unordered_set<std::uint32_t> g_formIdsWithActiveDots;

			// --- Timed debuffs (Frostbite, Sleep) — expiry processed on Actor::Update ---
			std::unordered_map<std::uint32_t, double> s_frostbiteUntilSec;
			std::unordered_map<std::uint32_t, bool> s_frostbiteStamDebuffApplied;
			std::unordered_map<std::uint32_t, double> s_sleepParalyzeUntilSec;
			std::unordered_set<std::uint32_t> g_formIdsWithTimedDebuffs;

			constexpr double kFrostbiteDurationSec = 30.0;
			constexpr double kSleepParalyzeDurationSec = 5.0;
			constexpr float kFrostbiteDamageTakenMult = 1.20f;
			constexpr float kFrostbiteStaminaRateMultDelta = -0.50f;

			void RefreshDotsMembership(std::uint32_t a_formId)
			{
				const bool any = s_poisonDots.contains(a_formId) || s_rotDots.contains(a_formId);
				if (any) {
					g_formIdsWithActiveDots.insert(a_formId);
				} else {
					g_formIdsWithActiveDots.erase(a_formId);
				}
			}

			void RefreshTimedMembership(std::uint32_t a_formId)
			{
				const bool any = s_frostbiteUntilSec.contains(a_formId) || s_sleepParalyzeUntilSec.contains(a_formId);
				if (any) {
					g_formIdsWithTimedDebuffs.insert(a_formId);
				} else {
					g_formIdsWithTimedDebuffs.erase(a_formId);
				}
			}

			struct MaxStats
			{
				float maxHP = 0.0f;
				float maxMP = 0.0f;
				float maxSP = 0.0f;
			};

			[[nodiscard]] MaxStats GetMaxStatsFromActor(RE::Actor* a_actor)
			{
				MaxStats out{};
				if (!a_actor) {
					return out;
				}
				if (a_actor->IsPlayerRef()) {
					ERLS_API::PlayerStatsSnapshot snap{};
					if (ERCF::ERLS::TryGetPlayerSnapshot(snap)) {
						out.maxHP = static_cast<float>(snap.derived.maxHP);
						out.maxMP = static_cast<float>(snap.derived.maxMP);
						out.maxSP = static_cast<float>(snap.derived.maxSP);
						return out;
					}
				}
				if (auto* av = a_actor->AsActorValueOwner()) {
					out.maxHP = av->GetPermanentActorValue(RE::ActorValue::kHealth);
					out.maxMP = av->GetPermanentActorValue(RE::ActorValue::kMagicka);
					out.maxSP = av->GetPermanentActorValue(RE::ActorValue::kStamina);
				}
				return out;
			}

			[[nodiscard]] float GetWeaponPhysicalDamageFromActor(RE::Actor* a_attacker)
			{
				if (!a_attacker) {
					return 0.0f;
				}
				const RE::InventoryEntryData* w = a_attacker->GetAttackingWeapon();
				if (!w || !w->GetObject()) {
					return 0.0f;
				}
				const auto* weap = w->GetObject()->As<RE::TESObjectWEAP>();
				if (!weap) {
					return 0.0f;
				}
				return weap->GetAttackDamage();
			}

			[[nodiscard]] bool TargetIsBossFallback(RE::Actor* a_target)
			{
				return a_target && a_target->HasKeywordString("ActorTypeBoss");
			}

			void ResolveRequirementFields(
				const StatusProcMessage& a_msg,
				RE::Actor* a_attacker,
				RE::Actor* a_target,
				float& a_weaponPhysOut,
				MaxStats& a_statsOut,
				bool& a_bossOut)
			{
				const bool snap =
					(a_msg.procContextFlags & kProcCtxRequirementSnapshotValid) != 0;
				if (snap && a_msg.targetMaxHp > 0.0f) {
					a_statsOut.maxHP = a_msg.targetMaxHp;
					a_statsOut.maxMP = a_msg.targetMaxMagicka;
					a_statsOut.maxSP = a_msg.targetMaxStamina;
				} else if (a_target) {
					a_statsOut = GetMaxStatsFromActor(a_target);
				} else {
					a_statsOut = {};
				}

				if (snap && a_msg.attackerWeaponPhysicalDamage > 0.0f) {
					a_weaponPhysOut = a_msg.attackerWeaponPhysicalDamage;
				} else {
					a_weaponPhysOut = GetWeaponPhysicalDamageFromActor(a_attacker);
				}

				if (snap) {
					a_bossOut = (a_msg.procContextFlags & kProcCtxTargetIsBoss) != 0;
				} else {
					a_bossOut = TargetIsBossFallback(a_target);
				}
			}

			void ApplyStatusProcMessageImpl(const StatusProcMessage& a_msg)
			{
				if (a_msg.targetFormId == 0) {
					ERCFLog::Line("ERCF: ApplyStatusProc failed: targetFormId=0");
					return;
				}
				if (a_msg.payloadAtPop <= 0.0f) {
					return;
				}

				auto* target = RE::TESForm::LookupByID<RE::Actor>(a_msg.targetFormId);
				if (!target) {
					ERCFLog::LineF("ERCF: ApplyStatusProc: target Actor not found (formId=%u)", a_msg.targetFormId);
					return;
				}
				if (target->IsDead()) {
					return;
				}

				auto* attacker = a_msg.attackerFormId != 0 ?
					RE::TESForm::LookupByID<RE::Actor>(a_msg.attackerFormId) :
					nullptr;

				float weaponPhys = 0.0f;
				MaxStats ms{};
				bool boss = false;
				ResolveRequirementFields(a_msg, attacker, target, weaponPhys, ms, boss);

				if (a_msg.statusId == Status_Bleed) {
					const float maxHP = ms.maxHP;
					const float hpTerm = (boss ? 0.075f : 0.15f) * maxHP;
					const float dmg = 0.10f * weaponPhys + hpTerm;
					if (!std::isfinite(dmg) || dmg <= 0.0f) {
						return;
					}
					if (auto* avOwner = target->AsActorValueOwner()) {
						avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -dmg);
					}
					return;
				}

				if (a_msg.statusId == Status_Poison) {
					const float maxHP = ms.maxHP;
					const float tick = (0.0007f * maxHP) + 7.0f;
					if (!std::isfinite(tick) || tick <= 0.0f) {
						return;
					}
					const double now = NowSeconds();
					{
						std::lock_guard<std::mutex> lock{ g_mutex };
						auto& st = s_poisonDots[target->GetFormID()];
						st.tickDamage = tick;
						st.ticksRemaining = 90;
						st.nextTickSec = now + 1.0;
						RefreshDotsMembership(target->GetFormID());
					}
					return;
				}

				if (a_msg.statusId == Status_Rot) {
					const float maxHP = ms.maxHP;
					const float tick = (0.0018f * maxHP) + 10.0f;
					if (!std::isfinite(tick) || tick <= 0.0f) {
						return;
					}
					const double now = NowSeconds();
					{
						std::lock_guard<std::mutex> lock{ g_mutex };
						auto& st = s_rotDots[target->GetFormID()];
						st.tickDamage = tick;
						st.ticksRemaining = 90;
						st.nextTickSec = now + 1.0;
						RefreshDotsMembership(target->GetFormID());
					}
					return;
				}

				if (a_msg.statusId == Status_Frostbite) {
					const float maxHP = ms.maxHP;
					const float hpTerm = (boss ? 0.07f : 0.10f) * maxHP;
					const float dmg = 0.05f * weaponPhys + hpTerm;
					if (!std::isfinite(dmg) || dmg <= 0.0f) {
						return;
					}
					if (auto* avOwner = target->AsActorValueOwner()) {
						avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -dmg);
						const double now = NowSeconds();
						std::lock_guard<std::mutex> lock{ g_mutex };
						s_frostbiteUntilSec[target->GetFormID()] = now + kFrostbiteDurationSec;
						if (!s_frostbiteStamDebuffApplied[target->GetFormID()]) {
							avOwner->ModActorValue(RE::ActorValue::kStaminaRateMult, kFrostbiteStaminaRateMultDelta);
							s_frostbiteStamDebuffApplied[target->GetFormID()] = true;
						}
						RefreshTimedMembership(target->GetFormID());
					}
					return;
				}

				if (a_msg.statusId == Status_Sleep) {
					if (auto* avOwner = target->AsActorValueOwner()) {
						const float mp = avOwner->GetActorValue(RE::ActorValue::kMagicka);
						if (mp > 0.0f) {
							avOwner->ModActorValue(RE::ActorValue::kMagicka, -mp);
						}
						avOwner->SetActorValue(RE::ActorValue::kParalysis, 1.0f);
						const double now = NowSeconds();
						std::lock_guard<std::mutex> lock{ g_mutex };
						s_sleepParalyzeUntilSec[target->GetFormID()] = now + kSleepParalyzeDurationSec;
						RefreshTimedMembership(target->GetFormID());
					}
					return;
				}

				if (a_msg.statusId == Status_Madness) {
					if (!target->IsPlayerRef()) {
						return;
					}
					if (auto* avOwner = target->AsActorValueOwner()) {
						const float maxHP = ms.maxHP;
						const float maxMP = ms.maxMP;
						const float hpDmg = (0.10f * maxHP) + 20.0f;
						const float mpDrain = (0.10f * maxMP) + 30.0f;
						if (hpDmg > 0.0f) {
							avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -hpDmg);
						}
						if (mpDrain > 0.0f) {
							avOwner->ModActorValue(RE::ActorValue::kMagicka, -mpDrain);
						}
					}
					return;
				}
			}

			void ProcessTimedDebuffExpiryForForm(std::uint32_t a_formId, RE::Actor* a_actor, double a_nowSec)
			{
				{
					const auto itF = s_frostbiteUntilSec.find(a_formId);
					if (itF != s_frostbiteUntilSec.end() && a_nowSec >= itF->second) {
						bool hadStamDebuff = false;
						const auto itStam = s_frostbiteStamDebuffApplied.find(a_formId);
						if (itStam != s_frostbiteStamDebuffApplied.end()) {
							hadStamDebuff = itStam->second;
							s_frostbiteStamDebuffApplied.erase(itStam);
						}
						s_frostbiteUntilSec.erase(itF);
						RefreshTimedMembership(a_formId);
						if (hadStamDebuff && a_actor) {
							if (auto* av = a_actor->AsActorValueOwner()) {
								av->ModActorValue(RE::ActorValue::kStaminaRateMult, -kFrostbiteStaminaRateMultDelta);
							}
						}
					}
				}
				{
					const auto itS = s_sleepParalyzeUntilSec.find(a_formId);
					if (itS != s_sleepParalyzeUntilSec.end() && a_nowSec >= itS->second) {
						s_sleepParalyzeUntilSec.erase(itS);
						RefreshTimedMembership(a_formId);
						if (a_actor) {
							if (auto* av = a_actor->AsActorValueOwner()) {
								av->SetActorValue(RE::ActorValue::kParalysis, 0.0f);
							}
						}
					}
				}
			}

			void TickPoisonRotDotsForForm(
				std::uint32_t a_formId,
				RE::Actor* a_target,
				double a_nowSec,
				const Config::Values& a_cfg)
			{
				(void)a_cfg;
				if (!a_target || a_target->IsDead()) {
					s_poisonDots.erase(a_formId);
					s_rotDots.erase(a_formId);
					RefreshDotsMembership(a_formId);
					return;
				}

				const auto runMap = [&](std::unordered_map<std::uint32_t, PoisonDotState>& map) {
					const auto it = map.find(a_formId);
					if (it == map.end()) {
						return;
					}
					auto& st = it->second;
					if (st.ticksRemaining == 0) {
						map.erase(it);
						RefreshDotsMembership(a_formId);
						return;
					}
					if (a_nowSec >= st.nextTickSec) {
						st.nextTickSec = a_nowSec + 1.0;
						st.ticksRemaining--;
						if (auto* avOwner = a_target->AsActorValueOwner()) {
							avOwner->RestoreActorValue(
								RE::ACTOR_VALUE_MODIFIER::kDamage,
								RE::ActorValue::kHealth,
								-st.tickDamage);
						}
						if (st.ticksRemaining == 0) {
							map.erase(it);
							RefreshDotsMembership(a_formId);
						}
					}
				};

				runMap(s_poisonDots);
				runMap(s_rotDots);
			}

			void FillProcRequirementSnapshotImpl(
				RE::Actor* a_attacker,
				RE::Actor* a_target,
				ProcRequirementSnapshot& a_out)
			{
				a_out = {};
				if (a_attacker) {
					a_out.attackerWeaponPhysicalDamage = GetWeaponPhysicalDamageFromActor(a_attacker);
				}
				if (a_target) {
					const MaxStats ms = GetMaxStatsFromActor(a_target);
					a_out.targetMaxHp = ms.maxHP;
					a_out.targetMaxMagicka = ms.maxMP;
					a_out.targetMaxStamina = ms.maxSP;
				}
			}

			[[nodiscard]] float GetDamageTakenMultiplierImpl(RE::FormID a_target)
			{
				const double now = NowSeconds();
				std::lock_guard<std::mutex> lock{ g_mutex };
				const auto it = s_frostbiteUntilSec.find(a_target);
				if (it != s_frostbiteUntilSec.end() && now < it->second) {
					return kFrostbiteDamageTakenMult;
				}
				return 1.0f;
			}

			// Only pause status decay on the title main menu. Do not use IsApplicationMenuOpen() or !ui here:
			// those are often true during normal play and would freeze meters incorrectly.
			[[nodiscard]] bool ShouldPauseStatusDecayForMainMenu()
			{
				auto* ui = RE::UI::GetSingleton();
				if (!ui) {
					return false;
				}
				return ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
			}

			// Per-(target FormID, status slot) decay registration; removed when that meter is 0.
			// Key: (formId << 8) | slot, slot 0..5 = Poison,Bleed,Rot,Frostbite,Sleep,Madness
			std::unordered_set<std::uint64_t> g_decayTimers;
			std::unordered_set<std::uint32_t> g_formIdsWithDecayMeters;
			// Wall-clock time through which we've integrated decay for this form (per-actor only; no global timer).
			// Not advanced while the actor has no 3D, so when 3D returns dt = now - this is the full offline gap.
			std::unordered_map<std::uint32_t, double> g_decayIntegratedThroughWall;

			[[nodiscard]] std::uint64_t MakeDecayKey(std::uint32_t a_formId, std::uint8_t a_slot)
			{
				return (static_cast<std::uint64_t>(a_formId) << 8) |
					static_cast<std::uint64_t>(a_slot & 0xFFu);
			}

			void DecodeDecayKey(std::uint64_t a_key, std::uint32_t& a_formIdOut, std::uint8_t& a_slotOut)
			{
				a_formIdOut = static_cast<std::uint32_t>(a_key >> 8);
				a_slotOut = static_cast<std::uint8_t>(a_key & 0xFFu);
			}

			[[nodiscard]] const char* DecaySlotName(std::uint8_t a_slot)
			{
				static const char* const kNames[] = {
					"Poison",
					"Bleed",
					"Rot",
					"Frostbite",
					"Sleep",
					"Madness",
				};
				if (a_slot < 6u) {
					return kNames[a_slot];
				}
				return "?";
			}

			[[nodiscard]] std::uint8_t SlotFromType(Type a_type)
			{
				switch (a_type) {
				case Type::Poison:
					return 0;
				case Type::Bleed:
					return 1;
				case Type::Rot:
					return 2;
				case Type::Frostbite:
					return 3;
				case Type::Sleep:
					return 4;
				case Type::Madness:
					return 5;
				default:
					return 0;
				}
			}

			[[nodiscard]] bool BindMeterBySlot(
				TargetState& st,
				std::uint8_t a_slot,
				float*& a_meterOut,
				double*& a_lastBuildupOut)
			{
				switch (a_slot) {
				case 0:
					a_meterOut = &st.poisonMeter;
					a_lastBuildupOut = &st.poisonLastBuildupSec;
					return true;
				case 1:
					a_meterOut = &st.bleedMeter;
					a_lastBuildupOut = &st.bleedLastBuildupSec;
					return true;
				case 2:
					a_meterOut = &st.rotMeter;
					a_lastBuildupOut = &st.rotLastBuildupSec;
					return true;
				case 3:
					a_meterOut = &st.frostbiteMeter;
					a_lastBuildupOut = &st.frostbiteLastBuildupSec;
					return true;
				case 4:
					a_meterOut = &st.sleepMeter;
					a_lastBuildupOut = &st.sleepLastBuildupSec;
					return true;
				case 5:
					a_meterOut = &st.madnessMeter;
					a_lastBuildupOut = &st.madnessLastBuildupSec;
					return true;
				default:
					return false;
				}
			}

			void RefreshFormDecayMembership(std::uint32_t a_formId)
			{
				bool any = false;
				for (std::uint8_t s = 0; s < 6; ++s) {
					if (g_decayTimers.contains(MakeDecayKey(a_formId, s))) {
						any = true;
						break;
					}
				}
				if (any) {
					g_formIdsWithDecayMeters.insert(a_formId);
				} else {
					g_formIdsWithDecayMeters.erase(a_formId);
					g_decayIntegratedThroughWall.erase(a_formId);
				}
			}

			void SyncDecayTimerForSlot(
				std::uint32_t a_formId,
				std::uint8_t a_slot,
				float a_meter,
				bool a_decayEnabled)
			{
				const std::uint64_t key = MakeDecayKey(a_formId, a_slot);
				if (!a_decayEnabled || a_meter <= 0.0f) {
					g_decayTimers.erase(key);
				} else {
					g_decayTimers.insert(key);
				}
				RefreshFormDecayMembership(a_formId);
			}

			[[nodiscard]] double NowSeconds()
			{
				using clock = std::chrono::steady_clock;
				using sec = std::chrono::duration<double>;
				return std::chrono::duration_cast<sec>(clock::now().time_since_epoch()).count();
			}

			void ApplyDecayOneMeter(
				float& a_meter,
				double a_lastBuildupSec,
				double a_t0,
				double a_t1,
				const Config::Values& a_cfg)
			{
				if (!a_cfg.status_meter_decay_enabled || a_meter <= 0.0f || a_lastBuildupSec <= 0.0) {
					return;
				}
				const double decayStart = a_lastBuildupSec + a_cfg.status_decay_delay_seconds;
				const double lo = (std::max)(a_t0, decayStart);
				const double hi = a_t1;
				if (hi <= lo) {
					return;
				}
				const double dur = hi - lo;
				// Universal linear decay: status_decay_rate = buildup points drained per real-time second.
				const double drained = a_cfg.status_decay_rate * dur;
				a_meter = static_cast<float>(
					(std::max)(0.0, static_cast<double>(a_meter) - drained));
				if (a_meter < 1e-5f) {
					a_meter = 0.0f;
				}
			}

			void ApplyDecayForSingleForm(
				std::uint32_t a_formId,
				double a_t0,
				double a_t1,
				const Config::Values& a_cfg,
				bool& a_outPlayerMetersHudChanged)
			{
				a_outPlayerMetersHudChanged = false;
				if (a_t1 <= a_t0) {
					return;
				}

				auto* pc = RE::PlayerCharacter::GetSingleton();
				const bool havePlayer = pc && pc->IsPlayerRef();
				const std::uint32_t playerFid = havePlayer ? pc->GetFormID() : 0u;

				std::lock_guard<std::mutex> lock{ g_mutex };

				PlayerMetersHudSnapshot before{};
				bool hadPlayerEntry = false;
				if (havePlayer) {
					const auto pit = g_targets.find(playerFid);
					if (pit != g_targets.end()) {
						hadPlayerEntry = true;
						const auto& st = pit->second;
						before.poison = st.poisonMeter;
						before.bleed = st.bleedMeter;
						before.rot = st.rotMeter;
						before.frostbite = st.frostbiteMeter;
						before.sleep = st.sleepMeter;
						before.madness = st.madnessMeter;
					}
				}

				for (std::uint8_t slot = 0; slot < 6; ++slot) {
					const std::uint64_t key = MakeDecayKey(a_formId, slot);
					if (!g_decayTimers.contains(key)) {
						continue;
					}

					const auto tit = g_targets.find(a_formId);
					if (tit == g_targets.end()) {
						g_decayTimers.erase(key);
						RefreshFormDecayMembership(a_formId);
						continue;
					}

					float* meter = nullptr;
					double* lastB = nullptr;
					if (!BindMeterBySlot(tit->second, slot, meter, lastB)) {
						g_decayTimers.erase(key);
						RefreshFormDecayMembership(a_formId);
						continue;
					}

					const float meterBefore = *meter;
					ApplyDecayOneMeter(*meter, *lastB, a_t0, a_t1, a_cfg);

					constexpr float kLogEps = 1e-5f;
					const bool decayed = (meterBefore > *meter + kLogEps) ||
						(meterBefore > kLogEps && *meter <= kLogEps);
					if (decayed && a_cfg.debug_status_decay) {
						double decayWindowSec = 0.0;
						if (*lastB > 0.0) {
							const double decayStart = *lastB + a_cfg.status_decay_delay_seconds;
							const double lo = (std::max)(a_t0, decayStart);
							const double hi = a_t1;
							if (hi > lo) {
								decayWindowSec = hi - lo;
							}
						}
						ERCFLog::LineF(
							"ERCF [StatusDecay] target=%08X %s before=%.4f after=%.4f decayWindowSec=%.4f",
							a_formId,
							DecaySlotName(slot),
							meterBefore,
							*meter,
							decayWindowSec);
					}

					if (*meter <= 0.0f) {
						*meter = 0.0f;
						g_decayTimers.erase(key);
						RefreshFormDecayMembership(a_formId);
					}
				}

				if (hadPlayerEntry && a_formId == playerFid) {
					const auto pit = g_targets.find(playerFid);
					if (pit != g_targets.end()) {
						const auto& st = pit->second;
						PlayerMetersHudSnapshot after{
							st.poisonMeter,
							st.bleedMeter,
							st.rotMeter,
							st.frostbiteMeter,
							st.sleepMeter,
							st.madnessMeter,
						};
						constexpr float kEps = 1e-3f;
						a_outPlayerMetersHudChanged =
							(std::fabs(after.poison - before.poison) > kEps) ||
							(std::fabs(after.bleed - before.bleed) > kEps) ||
							(std::fabs(after.rot - before.rot) > kEps) ||
							(std::fabs(after.frostbite - before.frostbite) > kEps) ||
							(std::fabs(after.sleep - before.sleep) > kEps) ||
							(std::fabs(after.madness - before.madness) > kEps);
					}
				}
			}

			void AccumulateAfterSync(
				float& a_meter,
				double& a_lastBuildupSec,
				float a_rawPayload,
				const Config::Values& a_cfg,
				double a_nowSec)
			{
				(void)a_cfg;
				a_meter = (std::max)(0.0f, a_meter) + (std::max)(0.0f, a_rawPayload);
				a_lastBuildupSec = a_nowSec;
			}

			[[nodiscard]] float GetMaxAV(RE::Actor* a_actor, RE::ActorValue a_av)
			{
				if (!a_actor) {
					return 0.0f;
				}
				if (auto* av = a_actor->AsActorValueOwner()) {
					return av->GetPermanentActorValue(a_av);
				}
				return 0.0f;
			}

			[[nodiscard]] float RobustnessCapForProcCount(std::uint32_t a_procCount)
			{
				if (a_procCount == 0) {
					return 300.0f;
				}
				if (a_procCount == 1) {
					return 450.0f;
				}
				return 600.0f;
			}

			void InitTimestampsIfNeeded(TargetState& st, double nowSec)
			{
				if (st.poisonLastBuildupSec == 0.0) {
					st.poisonLastBuildupSec = nowSec;
				}
				if (st.bleedLastBuildupSec == 0.0) {
					st.bleedLastBuildupSec = nowSec;
				}
				if (st.rotLastBuildupSec == 0.0) {
					st.rotLastBuildupSec = nowSec;
				}
				if (st.frostbiteLastBuildupSec == 0.0) {
					st.frostbiteLastBuildupSec = nowSec;
				}
				if (st.sleepLastBuildupSec == 0.0) {
					st.sleepLastBuildupSec = nowSec;
				}
				if (st.madnessLastBuildupSec == 0.0) {
					st.madnessLastBuildupSec = nowSec;
				}
			}

			void RunDeferredFlushImpl()
			{
				const auto cfg = Config::Get();
				const bool debugHits = cfg.debug_hit_events;

				bool buildupDirty = false;

				{
					std::lock_guard<std::mutex> lock{ g_mutex };
					buildupDirty = g_playerBuildupHudDirty.exchange(false, std::memory_order_acq_rel);
				}

				for (;;) {
					StatusProcBatchMessage batch{};
					{
						std::lock_guard<std::mutex> lock{ g_mutex };
						while (batch.count < kStatusProcBatchMaxEntries && !g_pendingPops.empty()) {
							batch.entries[batch.count++] = g_pendingPops.front();
							g_pendingPops.pop_front();
						}
					}
					if (batch.count == 0) {
						break;
					}
					if (debugHits) {
						for (std::uint32_t i = 0; i < batch.count; ++i) {
							const auto& e = batch.entries[i];
							ERCFLog::LineF(
								"ERCF [Hit] StatusProc emit status=%u attacker=%08X target=%08X meterBeforeHit=%.3f payload=%.3f",
								e.statusId,
								e.attackerFormId,
								e.targetFormId,
								e.meterBeforePop,
								e.payloadAtPop);
						}
					}
					for (std::uint32_t i = 0; i < batch.count; ++i) {
						ApplyStatusProcMessageImpl(batch.entries[i]);
					}
					EmitStatusProcBatch(batch);
				}

				if (!(cfg.enable_prisma_hud && buildupDirty)) {
					return;
				}

				auto* pc = RE::PlayerCharacter::GetSingleton();
				if (!pc || !pc->IsPlayerRef()) {
					return;
				}
				const std::uint32_t playerFid = pc->GetFormID();
				PlayerMetersHudSnapshot hud{};
				{
					std::lock_guard<std::mutex> lock{ g_mutex };
					const auto it = g_targets.find(playerFid);
					if (it != g_targets.end()) {
						const auto& st = it->second;
						hud.poison = st.poisonMeter;
						hud.bleed = st.bleedMeter;
						hud.rot = st.rotMeter;
						hud.frostbite = st.frostbiteMeter;
						hud.sleep = st.sleepMeter;
						hud.madness = st.madnessMeter;
					}
				}
				ERCFLog::LineF(
					"ERCF: Player meters poison=%.2f bleed=%.2f rot=%.2f frost=%.2f sleep=%.2f madness=%.2f",
					hud.poison,
					hud.bleed,
					hud.rot,
					hud.frostbite,
					hud.sleep,
					hud.madness);
				Prisma::OnPlayerBuildupHudEvent(
					hud.poison,
					hud.bleed,
					hud.rot,
					hud.frostbite,
					hud.sleep,
					hud.madness);
			}

			void QueueFlushTask()
			{
				bool expected = false;
				if (!g_flushQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
					return;
				}
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([]() {
						RunDeferredFlushImpl();
						g_flushQueued.store(false, std::memory_order_release);
					});
				} else {
					RunDeferredFlushImpl();
					g_flushQueued.store(false, std::memory_order_release);
				}
			}

			void PushPop(const StatusProcMessage& m)
			{
				g_pendingPops.push_back(m);
			}

			using ActorUpdateFn = void (*)(RE::Actor*, float);

			std::vector<std::pair<std::uintptr_t, ActorUpdateFn>> g_actorUpdateVtableOrig;
			std::atomic<bool> g_loggedDecayHook{ false };

			[[nodiscard]] ActorUpdateFn ResolveActorUpdateOriginal(RE::Actor* a_actor)
			{
				if (!a_actor) {
					return nullptr;
				}
				const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(a_actor);
				for (const auto& e : g_actorUpdateVtableOrig) {
					if (e.first == vtbl) {
						return e.second;
					}
				}
				return nullptr;
			}

			void OnActorPostUpdate(RE::Actor* a_actor)
			{
				if (!a_actor) {
					return;
				}
				const auto cfg = Config::Get();
				if (ShouldPauseStatusDecayForMainMenu()) {
					return;
				}

				const std::uint32_t fid = a_actor->GetFormID();
				if (fid == 0) {
					return;
				}

				bool runDecay = false;
				double t0 = 0.0;
				double t1 = 0.0;

				{
					std::lock_guard<std::mutex> lock{ g_mutex };
					const bool needDots = g_formIdsWithActiveDots.contains(fid);
					const bool needTimed = g_formIdsWithTimedDebuffs.contains(fid);
					const bool needDecay =
						cfg.status_meter_decay_enabled && g_formIdsWithDecayMeters.contains(fid);
					if (!needDots && !needTimed && !needDecay) {
						return;
					}

					t1 = NowSeconds();

					if (!a_actor->Is3DLoaded()) {
						return;
					}

					if (needTimed) {
						ProcessTimedDebuffExpiryForForm(fid, a_actor, t1);
					}
					if (needDots) {
						TickPoisonRotDotsForForm(fid, a_actor, t1, cfg);
					}

					if (needDecay) {
						double& integratedThrough = g_decayIntegratedThroughWall[fid];
						if (integratedThrough <= 0.0) {
							integratedThrough = t1;
							runDecay = false;
						} else {
							const double minInt =
								static_cast<double>((std::max)(0.05f, cfg.status_decay_tick_interval_seconds));
							if (t1 - integratedThrough < minInt) {
								runDecay = false;
							} else {
								t0 = integratedThrough;
								integratedThrough = t1;
								runDecay = true;
							}
						}
					}
				}

				if (runDecay) {
					if (!g_loggedDecayHook.exchange(true, std::memory_order_acq_rel) && cfg.debug_status_decay) {
						ERCFLog::Line(
							"ERCF [StatusDecay] Actor::Update decay active (verbose decay logging enabled in ercf.toml)");
					}

					bool hudDirty = false;
					ApplyDecayForSingleForm(fid, t0, t1, cfg, hudDirty);
					if (hudDirty && cfg.enable_prisma_hud) {
						g_playerBuildupHudDirty.store(true, std::memory_order_release);
						QueueFlushTask();
					}
				}
			}

			void ActorUpdateThunk(RE::Actor* a_this, float a_delta)
			{
				ActorUpdateFn orig = ResolveActorUpdateOriginal(a_this);
				if (!orig && !g_actorUpdateVtableOrig.empty()) {
					orig = g_actorUpdateVtableOrig.front().second;
				}
				if (orig) {
					orig(a_this, a_delta);
				} else {
					static std::atomic<bool> s_loggedMissingOrig{ false };
					if (!s_loggedMissingOrig.exchange(true, std::memory_order_acq_rel)) {
						ERCFLog::Line("ERCF: StatusEffects Actor::Update hook could not resolve original");
					}
				}
				OnActorPostUpdate(a_this);
			}

			void PatchActorUpdateVtables()
			{
				const std::size_t idx = REL::Relocate<std::size_t>(0xADu, 0xAFu);
				std::unordered_set<std::uintptr_t> patched{};
				auto patchOne = [&](REL::VariantID a_vid) {
					const auto addr = a_vid.address();
					if (!addr || !patched.insert(addr).second) {
						return;
					}
					REL::Relocation<std::uintptr_t> vtbl{ addr };
					const auto old = vtbl.write_vfunc(idx, &ActorUpdateThunk);
					if (old != 0) {
						g_actorUpdateVtableOrig.push_back(
							{ addr, reinterpret_cast<ActorUpdateFn>(old) });
					}
				};

				for (const auto& vid : RE::VTABLE_Actor) {
					patchOne(vid);
				}
				for (const auto& vid : RE::VTABLE_Character) {
					patchOne(vid);
				}
				for (const auto& vid : RE::VTABLE_PlayerCharacter) {
					patchOne(vid);
				}
			}

			void InstallActorDecayHookImpl()
			{
				static std::atomic<bool> s_installed{ false };
				if (s_installed.exchange(true, std::memory_order_acq_rel)) {
					return;
				}
				PatchActorUpdateVtables();
				ERCFLog::LineF(
					"ERCF: StatusEffects Actor::Update decay hook installed (vtbl origins=%zu)",
					g_actorUpdateVtableOrig.size());
			}
		}

		void InstallActorDecayHook()
		{
			InstallActorDecayHookImpl();
		}

		Thresholds ComputeThresholds(
			std::uint32_t a_targetFormId,
			RE::Actor* a_target,
			bool a_isPlayer,
			const Esp::StatusResistanceCoefficients& a_resist)
		{
			Thresholds out{};
			if (!a_target) {
				return out;
			}

			if (a_isPlayer) {
				ERLS_API::PlayerStatsSnapshot snap{};
				if (ERCF::ERLS::TryGetPlayerSnapshot(snap)) {
					out.immunity = snap.thresholds.immunity;
					out.robustness = snap.thresholds.robustness;
					out.focus = snap.thresholds.focus;
					out.madness = snap.thresholds.madness;
					return out;
				}
			}

			std::lock_guard<std::mutex> lock{ g_mutex };
			const auto it = g_targets.find(a_targetFormId);
			const TargetState* st = (it != g_targets.end()) ? &it->second : nullptr;
			const std::uint32_t robustnessProc = st ? st->robustnessProcCount : 0;

			const float maxHP = GetMaxAV(a_target, RE::ActorValue::kHealth);
			const float maxSP = GetMaxAV(a_target, RE::ActorValue::kStamina);
			const float maxMP = GetMaxAV(a_target, RE::ActorValue::kMagicka);

			out.immunity = (maxHP + maxSP) * 0.5f + a_resist.immunityResValue;

			const float robustnessBase = maxHP + a_resist.robustnessResValue;
			const float cap = RobustnessCapForProcCount(robustnessProc);
			out.robustness = (std::min)(robustnessBase, cap);

			out.focus = maxMP + a_resist.focusResValue;
			out.madness = (maxHP + maxMP) * 0.5f + a_resist.madnessResValue;
			return out;
		}

		void ProcStatusEffect(const ProcInput& a_input)
		{
			if (a_input.targetFormId == 0 || a_input.payload <= 0.0f) {
				return;
			}

			const auto cfg = Config::Get();
			const double nowSec = NowSeconds();
			auto* pc = RE::PlayerCharacter::GetSingleton();
			const bool targetIsPlayer = pc && pc->GetFormID() == a_input.targetFormId;

			std::lock_guard<std::mutex> lock{ g_mutex };
			auto& st = g_targets[a_input.targetFormId];
			InitTimestampsIfNeeded(st, nowSec);

			float* meter = nullptr;
			double* lastSec = nullptr;
			std::uint32_t statusId = 0;
			std::uint32_t band = 0;

			switch (a_input.type) {
			case Type::Poison:
				meter = &st.poisonMeter;
				lastSec = &st.poisonLastBuildupSec;
				statusId = Status_Poison;
				band = Band_Immunity;
				break;
			case Type::Rot:
				meter = &st.rotMeter;
				lastSec = &st.rotLastBuildupSec;
				statusId = Status_Rot;
				band = Band_Immunity;
				break;
			case Type::Bleed:
				meter = &st.bleedMeter;
				lastSec = &st.bleedLastBuildupSec;
				statusId = Status_Bleed;
				band = Band_Robustness;
				break;
			case Type::Frostbite:
				meter = &st.frostbiteMeter;
				lastSec = &st.frostbiteLastBuildupSec;
				statusId = Status_Frostbite;
				band = Band_Robustness;
				break;
			case Type::Sleep:
				meter = &st.sleepMeter;
				lastSec = &st.sleepLastBuildupSec;
				statusId = Status_Sleep;
				band = Band_Focus;
				break;
			case Type::Madness:
				meter = &st.madnessMeter;
				lastSec = &st.madnessLastBuildupSec;
				statusId = Status_Madness;
				band = Band_Madness;
				break;
			default:
				return;
			}

			const float before = *meter;
			AccumulateAfterSync(*meter, *lastSec, a_input.payload, cfg, nowSec);

			if (targetIsPlayer) {
				g_playerBuildupHudDirty.store(true, std::memory_order_release);
			}

			if (a_input.threshold > 0.0f && *meter >= a_input.threshold) {
				*meter = 0.0f;
				*lastSec = nowSec;

				switch (a_input.type) {
				case Type::Poison:
				case Type::Rot:
					st.immunityProcCount += 1;
					break;
				case Type::Bleed:
				case Type::Frostbite:
					st.robustnessProcCount += 1;
					break;
				case Type::Sleep:
					st.focusProcCount += 1;
					break;
				case Type::Madness:
					st.madnessProcCount += 1;
					break;
				default:
					break;
				}

				StatusProcMessage msg{};
				msg.attackerFormId = a_input.attackerFormId;
				msg.targetFormId = a_input.targetFormId;
				msg.statusId = statusId;
				msg.band = band;
				msg.meterBeforePop = before;
				msg.payloadAtPop = a_input.payload;
				msg.attackerWeaponPhysicalDamage = a_input.requirement.attackerWeaponPhysicalDamage;
				msg.targetMaxHp = a_input.requirement.targetMaxHp;
				msg.targetMaxMagicka = a_input.requirement.targetMaxMagicka;
				msg.targetMaxStamina = a_input.requirement.targetMaxStamina;
				msg.procContextFlags = kProcCtxRequirementSnapshotValid;
				if (a_input.targetIsBoss) {
					msg.procContextFlags |= kProcCtxTargetIsBoss;
				}
				PushPop(msg);

				if (targetIsPlayer) {
					switch (a_input.type) {
					case Type::Poison:
					case Type::Bleed:
					case Type::Rot:
					case Type::Frostbite:
					case Type::Sleep:
					case Type::Madness:
						Prisma::QueuePlayerStatusProcBanner(statusId);
						break;
					default:
						break;
					}
				}
			}

			SyncDecayTimerForSlot(
				a_input.targetFormId,
				SlotFromType(a_input.type),
				*meter,
				cfg.status_meter_decay_enabled);
		}

		void RequestDeferredFlush()
		{
			QueueFlushTask();
		}

		bool TryGetMeterSnapshotForHud(std::uint32_t a_targetFormId, PlayerMetersHudSnapshot& a_out)
		{
			a_out = {};
			if (a_targetFormId == 0) {
				return true;
			}

			std::lock_guard<std::mutex> lock{ g_mutex };
			const auto it = g_targets.find(a_targetFormId);
			if (it == g_targets.end()) {
				return true;
			}
			const auto& st = it->second;
			a_out.poison = st.poisonMeter;
			a_out.bleed = st.bleedMeter;
			a_out.rot = st.rotMeter;
			a_out.frostbite = st.frostbiteMeter;
			a_out.sleep = st.sleepMeter;
			a_out.madness = st.madnessMeter;
			return true;
		}

		bool TryGetPlayerMetersHudSnapshot(PlayerMetersHudSnapshot& a_out)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->IsPlayerRef()) {
				a_out = {};
				return false;
			}
			return TryGetMeterSnapshotForHud(player->GetFormID(), a_out);
		}

		bool TryGetPlayerMeterSnapshotForHud(float& poisonOut, float& bleedOut)
		{
			PlayerMetersHudSnapshot s{};
			if (!TryGetPlayerMetersHudSnapshot(s)) {
				poisonOut = 0.0f;
				bleedOut = 0.0f;
				return false;
			}
			poisonOut = s.poison;
			bleedOut = s.bleed;
			return true;
		}

		void FillProcRequirementSnapshot(
			RE::Actor* a_attacker,
			RE::Actor* a_target,
			ProcRequirementSnapshot& a_out)
		{
			FillProcRequirementSnapshotImpl(a_attacker, a_target, a_out);
		}

		void ApplyStatusProcMessage(const StatusProcMessage& a_msg)
		{
			ApplyStatusProcMessageImpl(a_msg);
		}

		float GetDamageTakenMultiplier(RE::FormID a_target)
		{
			return GetDamageTakenMultiplierImpl(a_target);
		}
	}
}
