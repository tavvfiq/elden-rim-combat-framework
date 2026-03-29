#pragma once

#include "Messaging.h"
#include <RE/T/TESForm.h>

namespace ERCF
{
	namespace Proc
	{
		// Damage-taken multiplier from active Frostbite debuff (30s). Defaults to 1.0.
		[[nodiscard]] float GetDamageTakenMultiplier(RE::FormID a_target);

		// Forwards to StatusEffects::ApplyStatusProcMessage (same requirement rules). ERCF applies procs
		// internally before emitting the public batch; use this for external tools re-playing messages.
		void ApplyStatusProc(const StatusProcMessage& a_msg);
	}
}

