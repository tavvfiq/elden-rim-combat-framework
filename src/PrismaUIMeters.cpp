#include "pch.h"

#include "PrismaUIMeters.h"

#include "Config.h"
#include "HitEventHandler.h"
#include "Messaging.h"
#include "PrismaUI_API.h"

#include "Log.h"

#include "RE/B/BSTEvent.h"
#include "RE/L/LoadingMenu.h"
#include "RE/M/MainMenu.h"
#include "RE/M/MenuOpenCloseEvent.h"
#include "RE/A/Actor.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/U/UI.h"
#include "SKSE/Interfaces.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>

namespace ERCF
{
	namespace Prisma
	{
		namespace
		{
			PRISMA_UI_API::IVPrismaUI1* g_api = nullptr;
			PrismaView g_view = 0;

			std::atomic<bool> g_domReady{ false };
			std::atomic<bool> g_initialized{ false };
			std::atomic<bool> g_hudPollDesired{ false };

			float g_poison = 0.0f;
			float g_bleed = 0.0f;
			float g_rot = 0.0f;
			float g_frostbite = 0.0f;
			float g_sleep = 0.0f;
			float g_madness = 0.0f;

			// Coalesce Prisma ops — ViewOperationQueue max ~100; Show+Interop every tick overflows quickly.
			bool g_prismaViewShown = false;
			float g_lastSentPoison = -1.0f;
			float g_lastSentBleed = -1.0f;
			float g_lastSentRot = -1.0f;
			float g_lastSentFrostbite = -1.0f;
			float g_lastSentSleep = -1.0f;
			float g_lastSentMadness = -1.0f;
			bool g_forceNextPrismaPush = false;
			bool g_forcePollHudPush = false;

			// steady_clock seconds — after on_dom_ready SKSE task finishes initial Interop+Hide (first Show gate).
			std::atomic<double> g_domInteropDoneWallSec{ 0.0 };
			// Centralized UI manager gate: coalesce all flush requests to one in-flight task.
			std::atomic<bool> g_hudFlushQueued{ false };

			constexpr const char* VIEW_PATH = "ERCF/index.html";
			// Coalesce stray hit-time updates; decay uses g_forcePollHudPush each poll tick instead.
			constexpr float kMeterSendEpsilon = 0.12f;
			constexpr float kMeterVisibleEpsilon = 1e-4f;
			constexpr double kProcBannerHoldSeconds = 3.5;
			// Fixed WebView2 pacing (not exposed in ercf.toml — Souls Style Looting uses no equivalent knobs).
			constexpr double kPrismaMinInteropIntervalSec = 0.2;
			constexpr double kPrismaGraceAfterDomSec = 2.0;

			std::mutex g_procBannerMutex;
			bool g_procBannerPending = false;
			std::string g_procBannerPendingKind;
			std::atomic<double> g_procBannerVisibleUntil{ 0.0 };

			[[nodiscard]] const char* BannerKindForErcfStatusId(std::uint32_t a_statusId)
			{
				switch (a_statusId) {
				case Status_Poison:
					return "poison";
				case Status_Bleed:
					return "bleed";
				case Status_Rot:
					return "rot";
				case Status_Frostbite:
					return "frostbite";
				case Status_Sleep:
					return "sleep";
				case Status_Madness:
					return "madness";
				default:
					return "";
				}
			}

			[[nodiscard]] double steady_now_sec()
			{
				using clock = std::chrono::steady_clock;
				using sec = std::chrono::duration<double>;
				return std::chrono::duration_cast<sec>(clock::now().time_since_epoch()).count();
			}

			[[nodiscard]] bool should_suppress_meters_hud()
			{
				auto* ui = RE::UI::GetSingleton();
				if (!ui) {
					return true;
				}
				if (ui->IsApplicationMenuOpen()) {
					return true;
				}
				// Title / load screens are not covered by IsApplicationMenuOpen on some builds.
				if (ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
					return true;
				}
				if (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
					return true;
				}
				auto* pc = RE::PlayerCharacter::GetSingleton();
				if (!pc) {
					return true;
				}
				return false;
			}

			float g_lastPublishedHudPoison = -1.0f;
			float g_lastPublishedHudBleed = -1.0f;
			float g_lastPublishedHudRot = -1.0f;
			float g_lastPublishedHudFrostbite = -1.0f;
			float g_lastPublishedHudSleep = -1.0f;
			float g_lastPublishedHudMadness = -1.0f;

			void ensure_hud_poll_only();
			void create_view_immediate();
			bool try_update();
			void request_hud_flush();

			struct PlayerHudSnapshot
			{
				float poison = 0.0f;
				float bleed = 0.0f;
				float rot = 0.0f;
				float frostbite = 0.0f;
				float sleep = 0.0f;
				float madness = 0.0f;
			};

			[[nodiscard]] bool PublishHudSnapshotIfChanged(
				const PlayerHudSnapshot& a_next,
				bool a_force,
				bool a_fromPoll)
			{
				constexpr float kFuzz = 1e-3f;
				const bool haveBaseline = g_lastPublishedHudPoison >= 0.0f;
				const bool changed =
					a_force ||
					!haveBaseline ||
					(std::fabs(a_next.poison - g_lastPublishedHudPoison) > kFuzz ||
						std::fabs(a_next.bleed - g_lastPublishedHudBleed) > kFuzz ||
						std::fabs(a_next.rot - g_lastPublishedHudRot) > kFuzz ||
						std::fabs(a_next.frostbite - g_lastPublishedHudFrostbite) > kFuzz ||
						std::fabs(a_next.sleep - g_lastPublishedHudSleep) > kFuzz ||
						std::fabs(a_next.madness - g_lastPublishedHudMadness) > kFuzz);
				if (!changed) {
					return false;
				}

				g_lastPublishedHudPoison = a_next.poison;
				g_lastPublishedHudBleed = a_next.bleed;
				g_lastPublishedHudRot = a_next.rot;
				g_lastPublishedHudFrostbite = a_next.frostbite;
				g_lastPublishedHudSleep = a_next.sleep;
				g_lastPublishedHudMadness = a_next.madness;

				g_poison = a_next.poison;
				g_bleed = a_next.bleed;
				g_rot = a_next.rot;
				g_frostbite = a_next.frostbite;
				g_sleep = a_next.sleep;
				g_madness = a_next.madness;
				// Force an immediate Interop batch (bypasses the minGap throttle).
				g_forceNextPrismaPush = true;
				if (a_fromPoll) {
					g_forcePollHudPush = true;
				}
				request_hud_flush();
				return true;
			}

			void reset_hud_publish_baselines()
			{
				g_lastPublishedHudPoison = -1.0f;
				g_lastPublishedHudBleed = -1.0f;
				g_lastPublishedHudRot = -1.0f;
				g_lastPublishedHudFrostbite = -1.0f;
				g_lastPublishedHudSleep = -1.0f;
				g_lastPublishedHudMadness = -1.0f;
			}

			void clear_hud_meter_state_no_prisma()
			{
				g_poison = 0.0f;
				g_bleed = 0.0f;
				g_rot = 0.0f;
				g_frostbite = 0.0f;
				g_sleep = 0.0f;
				g_madness = 0.0f;
				g_forceNextPrismaPush = false;
				g_forcePollHudPush = false;
				g_lastSentPoison = -1.0f;
				g_lastSentBleed = -1.0f;
				g_lastSentRot = -1.0f;
				g_lastSentFrostbite = -1.0f;
				g_lastSentSleep = -1.0f;
				g_lastSentMadness = -1.0f;
				reset_hud_publish_baselines();
				g_hudFlushQueued.store(false, std::memory_order_release);
				{
					std::lock_guard<std::mutex> lock{ g_procBannerMutex };
					g_procBannerPending = false;
					g_procBannerPendingKind.clear();
				}
				g_procBannerVisibleUntil.store(0.0, std::memory_order_release);
			}

			void prisma_clear_proc_banner_dom()
			{
				if (!g_api || !g_view || !g_domReady.load() || !g_api->IsValid(g_view)) {
					return;
				}
				g_api->InteropCall(g_view, "clearProcBanner", "");
			}

			// WebView must receive explicit zeros or rows stay visible in the DOM across Hide/Show.
			void push_meters_zero_to_dom_if_ready()
			{
				if (!g_api || !g_view || !g_domReady.load() || !g_api->IsValid(g_view)) {
					return;
				}
				g_api->InteropCall(g_view, "setPoisonMeter", "0");
				g_api->InteropCall(g_view, "setBleedMeter", "0");
				g_api->InteropCall(g_view, "setRotMeter", "0");
				g_api->InteropCall(g_view, "setFrostbiteMeter", "0");
				g_api->InteropCall(g_view, "setSleepMeter", "0");
				g_api->InteropCall(g_view, "setMadnessMeter", "0");
				g_lastSentPoison = 0.0f;
				g_lastSentBleed = 0.0f;
				g_lastSentRot = 0.0f;
				g_lastSentFrostbite = 0.0f;
				g_lastSentSleep = 0.0f;
				g_lastSentMadness = 0.0f;
			}

			void prisma_hide_overlay_on_game_thread()
			{
				if (!g_api || !g_view || !g_domReady.load()) {
					g_prismaViewShown = false;
					return;
				}
				if (!g_api->IsValid(g_view)) {
					g_prismaViewShown = false;
					return;
				}
				push_meters_zero_to_dom_if_ready();
				prisma_clear_proc_banner_dom();
				if (g_prismaViewShown) {
					g_api->Hide(g_view);
				}
				g_prismaViewShown = false;
			}

			void force_hide_prisma_overlay()
			{
				clear_hud_meter_state_no_prisma();
				prisma_hide_overlay_on_game_thread();
			}

			void stop_hud_poll_and_hide()
			{
				g_hudPollDesired.store(false, std::memory_order_release);
				g_domInteropDoneWallSec.store(0.0, std::memory_order_release);
				clear_hud_meter_state_no_prisma();
				// Never call Prisma Hide from MenuOpenCloseEvent / other BST sinks (re-entrancy freeze).
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([]() { prisma_hide_overlay_on_game_thread(); });
				} else {
					prisma_hide_overlay_on_game_thread();
				}
			}

			struct MainMenuHudSink final : RE::BSTEventSink<RE::MenuOpenCloseEvent>
			{
				RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
					RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
				{
					if (!a_event) {
						return RE::BSEventNotifyControl::kContinue;
					}
					if (a_event->opening && a_event->menuName == RE::MainMenu::MENU_NAME) {
						stop_hud_poll_and_hide();
					}
					// If kPostLoadGame never fires, poll may have stopped at title; restart when load finishes.
					// View already exists (Souls-style kDataLoaded path) — do not CreateView here.
					if (!a_event->opening && a_event->menuName == RE::LoadingMenu::MENU_NAME && g_view != 0) {
						// ensure_hud_poll_only();
					}
					return RE::BSEventNotifyControl::kContinue;
				}
			};

			void register_main_menu_sink_once()
			{
				static MainMenuHudSink s_sink;
				static bool s_registered = false;
				if (s_registered) {
					return;
				}
				auto* ui = RE::UI::GetSingleton();
				if (!ui) {
					return;
				}
				ui->AddEventSink<RE::MenuOpenCloseEvent>(std::addressof(s_sink));
				s_registered = true;
			}

			void on_dom_ready(PrismaView view)
			{
				g_view = view;
				// Prisma invokes this from its own thread; defer Show/Interop/Hide to SKSE game thread.
				g_domReady.store(false, std::memory_order_release);
				g_prismaViewShown = false;
				g_lastSentPoison = -1.0f;
				g_lastSentBleed = -1.0f;
				g_lastSentRot = -1.0f;
				g_lastSentFrostbite = -1.0f;
				g_lastSentSleep = -1.0f;
				g_lastSentMadness = -1.0f;

				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([]() {
						g_domReady.store(true, std::memory_order_release);
						g_prismaViewShown = false;
						g_lastSentPoison = -1.0f;
						g_lastSentBleed = -1.0f;
						g_lastSentRot = -1.0f;
						g_lastSentFrostbite = -1.0f;
						g_lastSentSleep = -1.0f;
						g_lastSentMadness = -1.0f;
						if (g_api && g_view && g_api->IsValid(g_view)) {
							const std::string poisonArg = std::to_string(g_poison);
							const std::string bleedArg = std::to_string(g_bleed);
							const std::string rotArg = std::to_string(g_rot);
							const std::string frostArg = std::to_string(g_frostbite);
							const std::string sleepArg = std::to_string(g_sleep);
							const std::string madnessArg = std::to_string(g_madness);
							g_api->InteropCall(g_view, "setPoisonMeter", poisonArg.c_str());
							g_api->InteropCall(g_view, "setBleedMeter", bleedArg.c_str());
							g_api->InteropCall(g_view, "setRotMeter", rotArg.c_str());
							g_api->InteropCall(g_view, "setFrostbiteMeter", frostArg.c_str());
							g_api->InteropCall(g_view, "setSleepMeter", sleepArg.c_str());
							g_api->InteropCall(g_view, "setMadnessMeter", madnessArg.c_str());
							g_api->InteropCall(g_view, "clearProcBanner", "");
							g_api->Hide(g_view);
						}
						using clock = std::chrono::steady_clock;
						using sec = std::chrono::duration<double>;
						const double t =
							std::chrono::duration_cast<sec>(clock::now().time_since_epoch()).count();
						g_domInteropDoneWallSec.store(t, std::memory_order_release);
					});
				} else {
					g_domReady.store(true, std::memory_order_release);
					if (g_api && g_view && g_api->IsValid(g_view)) {
						const std::string poisonArg = std::to_string(g_poison);
						const std::string bleedArg = std::to_string(g_bleed);
						const std::string rotArg = std::to_string(g_rot);
						const std::string frostArg = std::to_string(g_frostbite);
						const std::string sleepArg = std::to_string(g_sleep);
						const std::string madnessArg = std::to_string(g_madness);
						g_api->InteropCall(g_view, "setPoisonMeter", poisonArg.c_str());
						g_api->InteropCall(g_view, "setBleedMeter", bleedArg.c_str());
						g_api->InteropCall(g_view, "setRotMeter", rotArg.c_str());
						g_api->InteropCall(g_view, "setFrostbiteMeter", frostArg.c_str());
						g_api->InteropCall(g_view, "setSleepMeter", sleepArg.c_str());
						g_api->InteropCall(g_view, "setMadnessMeter", madnessArg.c_str());
						g_api->InteropCall(g_view, "clearProcBanner", "");
						g_api->Hide(g_view);
					}
					using clock = std::chrono::steady_clock;
					using sec = std::chrono::duration<double>;
					const double t =
						std::chrono::duration_cast<sec>(clock::now().time_since_epoch()).count();
					g_domInteropDoneWallSec.store(t, std::memory_order_release);
				}
			}

			bool try_update()
			{
				const auto& cfg = ERCF::Config::Get();
				if (!cfg.enable_prisma_hud) {
					return false;
				}
				if (!g_api) {
					return false;
				}

				if (should_suppress_meters_hud()) {
					// Never touch WebView2 while menus/loading are open.
					g_prismaViewShown = false;
					g_lastSentPoison = -1.0f;
					g_lastSentBleed = -1.0f;
					g_lastSentRot = -1.0f;
					g_lastSentFrostbite = -1.0f;
					g_lastSentSleep = -1.0f;
					g_lastSentMadness = -1.0f;
					g_forceNextPrismaPush = false;
					g_forcePollHudPush = false;
					return false;
				}

				const bool anyMeter =
					(g_poison > kMeterVisibleEpsilon || g_bleed > kMeterVisibleEpsilon ||
						g_rot > kMeterVisibleEpsilon || g_frostbite > kMeterVisibleEpsilon ||
						g_sleep > kMeterVisibleEpsilon || g_madness > kMeterVisibleEpsilon);

				if (!g_view) {
					return false;
				}
				if (!g_domReady.load()) {
					return false;
				}
				if (!g_api->IsValid(g_view)) {
					return false;
				}

				const double nowSec = steady_now_sec();

				// End of proc-banner hold window.
				const double bannerUntil = g_procBannerVisibleUntil.load(std::memory_order_acquire);
				if (bannerUntil > 0.0 && nowSec >= bannerUntil) {
					prisma_clear_proc_banner_dom();
					g_procBannerVisibleUntil.store(0.0, std::memory_order_release);
				}

				// New proc banner (bars at 0): top Elden-style strip only.
				std::string bannerKind;
				{
					std::lock_guard<std::mutex> lock{ g_procBannerMutex };
					if (g_procBannerPending) {
						bannerKind = std::move(g_procBannerPendingKind);
						g_procBannerPending = false;
					}
				}
				if (!bannerKind.empty()) {
					if (!g_prismaViewShown) {
						g_api->Show(g_view);
						g_prismaViewShown = true;
					}
					g_api->InteropCall(g_view, "showStatusProcBanner", bannerKind.c_str());
					g_api->InteropCall(g_view, "setPoisonMeter", "0");
					g_api->InteropCall(g_view, "setBleedMeter", "0");
					g_api->InteropCall(g_view, "setRotMeter", "0");
					g_api->InteropCall(g_view, "setFrostbiteMeter", "0");
					g_api->InteropCall(g_view, "setSleepMeter", "0");
					g_api->InteropCall(g_view, "setMadnessMeter", "0");
					g_lastSentPoison = 0.0f;
					g_lastSentBleed = 0.0f;
					g_lastSentRot = 0.0f;
					g_lastSentFrostbite = 0.0f;
					g_lastSentSleep = 0.0f;
					g_lastSentMadness = 0.0f;
					g_forceNextPrismaPush = false;
					g_forcePollHudPush = false;
					g_procBannerVisibleUntil.store(nowSec + kProcBannerHoldSeconds, std::memory_order_release);
					return true;
				}

				const bool bannerHoldActive =
					g_procBannerVisibleUntil.load(std::memory_order_acquire) > nowSec;

				if (!anyMeter && !bannerHoldActive) {
					if (g_prismaViewShown) {
						push_meters_zero_to_dom_if_ready();
						g_api->Hide(g_view);
						g_prismaViewShown = false;
					}
					g_lastSentPoison = -1.0f;
					g_lastSentBleed = -1.0f;
					g_lastSentRot = -1.0f;
					g_lastSentFrostbite = -1.0f;
					g_lastSentSleep = -1.0f;
					g_lastSentMadness = -1.0f;
					g_forceNextPrismaPush = false;
					g_forcePollHudPush = false;
					return false;
				}

				if (!anyMeter && bannerHoldActive) {
					push_meters_zero_to_dom_if_ready();
					return g_prismaViewShown;
				}

				const bool pollPushThisTick = g_forcePollHudPush;
				g_forcePollHudPush = false;

				const auto crossed = [](float last, float cur) {
					return (last <= kMeterVisibleEpsilon && cur > kMeterVisibleEpsilon) ||
						(last > kMeterVisibleEpsilon && cur <= kMeterVisibleEpsilon);
				};
				const auto moved = [](float last, float cur) {
					return (last < 0.0f) || (std::fabs(cur - last) > kMeterSendEpsilon);
				};
				const bool crossedZeroPoison = crossed(g_lastSentPoison, g_poison);
				const bool crossedZeroBleed = crossed(g_lastSentBleed, g_bleed);
				const bool crossedZeroRot = crossed(g_lastSentRot, g_rot);
				const bool crossedZeroFrost = crossed(g_lastSentFrostbite, g_frostbite);
				const bool crossedZeroSleep = crossed(g_lastSentSleep, g_sleep);
				const bool crossedZeroMadness = crossed(g_lastSentMadness, g_madness);
				const bool poisonMoved = moved(g_lastSentPoison, g_poison);
				const bool bleedMoved = moved(g_lastSentBleed, g_bleed);
				const bool rotMoved = moved(g_lastSentRot, g_rot);
				const bool frostMoved = moved(g_lastSentFrostbite, g_frostbite);
				const bool sleepMoved = moved(g_lastSentSleep, g_sleep);
				const bool madnessMoved = moved(g_lastSentMadness, g_madness);
				const bool forcedHitPush = g_forceNextPrismaPush;
				const bool anyCrossedZero = crossedZeroPoison || crossedZeroBleed || crossedZeroRot ||
					crossedZeroFrost || crossedZeroSleep || crossedZeroMadness;
				const bool needInterop = forcedHitPush || pollPushThisTick || anyCrossedZero ||
					poisonMoved || bleedMoved || rotMoved || frostMoved || sleepMoved || madnessMoved;

				if (!needInterop) {
					return g_prismaViewShown;
				}

				const double domDone = g_domInteropDoneWallSec.load(std::memory_order_relaxed);
				if (!forcedHitPush && !g_prismaViewShown && domDone > 0.0 && kPrismaGraceAfterDomSec > 0.0 &&
					(nowSec - domDone) < kPrismaGraceAfterDomSec) {
					return g_prismaViewShown;
				}

				static double s_lastShowInteropSec = 0.0;
				const double minGap = (std::max)(0.05, kPrismaMinInteropIntervalSec);
				if (!forcedHitPush && !anyCrossedZero && minGap > 0.0 &&
					(nowSec - s_lastShowInteropSec) < minGap) {
					return g_prismaViewShown;
				}

				g_forceNextPrismaPush = false;

				if (!g_prismaViewShown) {
					g_api->Show(g_view);
					g_prismaViewShown = true;
				}

				const std::string poisonArg = std::to_string(g_poison);
				const std::string bleedArg = std::to_string(g_bleed);
				const std::string rotArg = std::to_string(g_rot);
				const std::string frostArg = std::to_string(g_frostbite);
				const std::string sleepArg = std::to_string(g_sleep);
				const std::string madnessArg = std::to_string(g_madness);
				g_api->InteropCall(g_view, "setPoisonMeter", poisonArg.c_str());
				g_api->InteropCall(g_view, "setBleedMeter", bleedArg.c_str());
				g_api->InteropCall(g_view, "setRotMeter", rotArg.c_str());
				g_api->InteropCall(g_view, "setFrostbiteMeter", frostArg.c_str());
				g_api->InteropCall(g_view, "setSleepMeter", sleepArg.c_str());
				g_api->InteropCall(g_view, "setMadnessMeter", madnessArg.c_str());
				g_lastSentPoison = g_poison;
				g_lastSentBleed = g_bleed;
				g_lastSentRot = g_rot;
				g_lastSentFrostbite = g_frostbite;
				g_lastSentSleep = g_sleep;
				g_lastSentMadness = g_madness;
				s_lastShowInteropSec = nowSec;
				return true;
			}

			void queue_player_status_proc_banner(std::uint32_t a_statusId)
			{
				const auto& cfg = ERCF::Config::Get();
				if (!cfg.enable_prisma_hud || !g_api) {
					return;
				}
				const char* kind = BannerKindForErcfStatusId(a_statusId);
				if (!kind || !*kind) {
					return;
				}
				{
					std::lock_guard<std::mutex> lock{ g_procBannerMutex };
					g_procBannerPending = true;
					g_procBannerPendingKind = kind;
				}
				request_hud_flush();
			}

			void flush_hud_once()
			{
				g_hudFlushQueued.store(false, std::memory_order_release);
				try_update();
			}

			void request_hud_flush()
			{
				bool expected = false;
				if (!g_hudFlushQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
					return;
				}
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([]() { flush_hud_once(); });
				} else {
					flush_hud_once();
				}
			}

			void schedule_hud_poll()
			{
				if (!g_hudPollDesired.load(std::memory_order_acquire)) {
					return;
				}
				auto* tasks = SKSE::GetTaskInterface();
				if (!tasks) {
					return;
				}
				tasks->AddTask([]() {
					if (!g_hudPollDesired.load(std::memory_order_acquire)) {
						return;
					}
					const auto& cfg = ERCF::Config::Get();
					using clock = std::chrono::steady_clock;
					using sec = std::chrono::duration<double>;
					const double nowSec =
						std::chrono::duration_cast<sec>(clock::now().time_since_epoch()).count();

					static double s_lastDecayRefreshSec = 0.0;

					// Menus / title: try_update throttled — still responsive without hammering WebView2.
					if (should_suppress_meters_hud()) {
						// IMPORTANT: do not keep polling on menus.
						// This task runs on the SKSE task thread and re-scheduling it every frame while
						// MainMenu/LoadingMenu are open can hard-freeze some setups (WebView2/interop path).
						// We'll restart polling when menus close (see MenuOpenCloseEvent sink).
						if (g_prismaViewShown) {
							prisma_hide_overlay_on_game_thread();
						}
						g_hudPollDesired.store(false, std::memory_order_release);
						return;
					}

					// Decay / drift only (buildup uses OnPlayerBuildupHudEvent from hit-deferred task).
					const double pollSec = static_cast<double>(cfg.prisma_hud_poll_interval_seconds);
					if (pollSec > 0.0) {
						const bool due =
							s_lastDecayRefreshSec == 0.0 || (nowSec - s_lastDecayRefreshSec) >= pollSec;
						if (due) {
							s_lastDecayRefreshSec = nowSec;
							PlayerHudSnapshot snap{};
							StatusEffects::PlayerMetersHudSnapshot m{};
							if (ERCF::Runtime::TryGetPlayerMetersHudSnapshot(m)) {
								snap.poison = m.poison;
								snap.bleed = m.bleed;
								snap.rot = m.rot;
								snap.frostbite = m.frostbite;
								snap.sleep = m.sleep;
								snap.madness = m.madness;
							}
							// Poll is the decay/drift reconcile path — only push if changed.
							if (PublishHudSnapshotIfChanged(snap, false, true)) {
								request_hud_flush();
							}
						}
					}

					// Proc-banner expiry is handled inside try_update(); flush must run on the poll tick even when
					// meter snapshots are unchanged, or the DOM stays visible until the next unrelated HUD event.
					if (g_procBannerVisibleUntil.load(std::memory_order_acquire) > 0.0) {
						request_hud_flush();
					}

					if (g_hudPollDesired.load(std::memory_order_acquire)) {
						schedule_hud_poll();
					}
				});
			}

			void create_view_immediate()
			{
				if (!ERCF::Config::Get().enable_prisma_hud || !g_api || g_view != 0) {
					return;
				}
				g_view = g_api->CreateView(VIEW_PATH, on_dom_ready);
				if (!g_view) {
					ERCFLog::Line("ERCF: PrismaUI CreateView failed (kDataLoaded)");
					return;
				}
				ERCFLog::Line("ERCF: PrismaUI view created on kDataLoaded (Souls-style)");
			}

			// Starts SKSE poll chain (meter snapshots + proc-banner expiry). CreateView already ran on kDataLoaded.
			void ensure_hud_poll_only()
			{
				if (!ERCF::Config::Get().enable_prisma_hud || !g_api) {
					return;
				}
				const bool wasRunning = g_hudPollDesired.exchange(true, std::memory_order_acq_rel);
				if (!wasRunning) {
					ERCFLog::Line("ERCF: Prisma HUD poll started (meters)");
					schedule_hud_poll();
				}
			}
		}

		void Init()
		{
			if (g_initialized.exchange(true)) {
				return;
			}

			const auto& cfg = ERCF::Config::Get();
			if (!cfg.enable_prisma_hud) {
				ERCFLog::Line("ERCF: Prisma HUD disabled (enable_prisma_hud = false in ercf.toml)");
				return;
			}

			g_api = reinterpret_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));
			if (!g_api) {
				ERCFLog::Line("ERCF: PrismaUI_API not found (PrismaUI.dll missing?)");
				return;
			}

			register_main_menu_sink_once();

			// Souls Style Looting pattern: CreateView from kDataLoaded after RequestPluginAPI.
			create_view_immediate();
			QueueStartPeriodicHudRefresh();
			ERCFLog::Line("ERCF: PrismaUI API acquired (CreateView on kDataLoaded)");
		}

		void StartPeriodicHudRefresh()
		{
			if (!ERCF::Config::Get().enable_prisma_hud) {
				return;
			}
			if (!g_api) {
				return;
			}
			ensure_hud_poll_only();
			ERCFLog::Line("ERCF: Prisma: session HUD refresh (poll armed)");
		}

		void StopPeriodicHudRefresh()
		{
			stop_hud_poll_and_hide();
		}

		void QueueStartPeriodicHudRefresh()
		{
			const auto& cfg = ERCF::Config::Get();
			if (!cfg.enable_prisma_hud) {
				return;
			}
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([]() { StartPeriodicHudRefresh(); });
			} else {
				StartPeriodicHudRefresh();
			}
		}

		void OnPlayerBuildupHudEvent(
			float poisonForHud,
			float bleedForHud,
			float rotForHud,
			float frostbiteForHud,
			float sleepForHud,
			float madnessForHud)
		{
			const auto& cfg = ERCF::Config::Get();
			if (!cfg.enable_prisma_hud || !g_api) {
				return;
			}

			PlayerHudSnapshot snap{};
			snap.poison = poisonForHud;
			snap.bleed = bleedForHud;
			snap.rot = rotForHud;
			snap.frostbite = frostbiteForHud;
			snap.sleep = sleepForHud;
			snap.madness = madnessForHud;
			if (!PublishHudSnapshotIfChanged(snap, false, false)) {
				return;
			}
			// Snapshot submit only — manager coalesces and schedules a safe flush.
		}

		void OnPrismaHudBuildupMessaging()
		{
			// Backward-compat trigger: route through centralized manager.
			request_hud_flush();
		}

		void QueuePlayerStatusProcBanner(std::uint32_t a_statusId)
		{
			queue_player_status_proc_banner(a_statusId);
		}
	}
}

