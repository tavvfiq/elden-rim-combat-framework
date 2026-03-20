#pragma once

#include "Messaging.h"

namespace ERCF
{
	namespace Proc
	{
		// Applies Poison/Bleed proc numeric effects when a public ERCF.StatusProc message arrives.
		// v1 behavior: treat `payloadAtPop` as flat HP damage and apply it immediately.
		void ApplyStatusProc(const StatusProcMessage& a_msg);
	}
}

