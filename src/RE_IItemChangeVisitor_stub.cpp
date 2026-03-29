// CommonLibSSE-NG declares RE::InventoryChanges::IItemChangeVisitor::~IItemChangeVisitor()
// but (in some revisions) does not provide a definition. Any derived visitor (e.g. EspRouting's
// WornArmorVisitor) needs this symbol at link time.
#include "pch.h"

#include <RE/I/InventoryChanges.h>

namespace RE
{
	InventoryChanges::IItemChangeVisitor::~IItemChangeVisitor() = default;
}
