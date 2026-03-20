#include "pch.h"

#include "PrismaUIMeters.h"

#include "PrismaUI_API.h"

#include "Log.h"

#include <atomic>
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

			float g_poison = 0.0f;
			float g_bleed = 0.0f;

			constexpr const char* VIEW_PATH = "ERCF/index.html";

			void on_dom_ready(PrismaView view)
			{
				g_view = view;
				g_domReady = true;

				if (g_api && g_view) {
					const std::string poisonArg = std::to_string(g_poison);
					const std::string bleedArg = std::to_string(g_bleed);
					// Ensure visible on init.
					g_api->Show(g_view);
					g_api->InteropCall(g_view, "setPoisonMeter", poisonArg.c_str());
					g_api->InteropCall(g_view, "setBleedMeter", bleedArg.c_str());
				}
			}

			bool try_update()
			{
				if (!g_api || !g_view) return false;
				if (!g_domReady.load()) return false;

				// PrismaUI interop uses const char* argument. Keep strings alive for the duration of calls.
				const std::string poisonArg = std::to_string(g_poison);
				const std::string bleedArg = std::to_string(g_bleed);
				g_api->InteropCall(g_view, "setPoisonMeter", poisonArg.c_str());
				g_api->InteropCall(g_view, "setBleedMeter", bleedArg.c_str());
				return true;
			}
		}

		void Init()
		{
			if (g_initialized.exchange(true)) {
				return;
			}

			g_api = reinterpret_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));
			if (!g_api) {
				ERCFLog::Line("ERCF: PrismaUI_API not found (PrismaUI.dll missing?)");
				return;
			}

			g_view = g_api->CreateView(VIEW_PATH, on_dom_ready);
			if (!g_view) {
				ERCFLog::Line("ERCF: PrismaUI CreateView failed");
				return;
			}

			ERCFLog::Line("ERCF: PrismaUI view created");
			try_update();
		}

		void UpdateMeters(float poisonMeter, float bleedMeter)
		{
			g_poison = poisonMeter;
			g_bleed = bleedMeter;
			try_update();
		}
	}
}

