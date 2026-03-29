#include "pch.h"

#include "Proc.h"

#include "StatusEffectManager.h"

namespace ERCF
{
	namespace Proc
	{
		float GetDamageTakenMultiplier(RE::FormID a_target)
		{
			return StatusEffects::GetDamageTakenMultiplier(a_target);
		}

		void ApplyStatusProc(const StatusProcMessage& a_msg)
		{
			StatusEffects::ApplyStatusProcMessage(a_msg);
		}
	}
}
