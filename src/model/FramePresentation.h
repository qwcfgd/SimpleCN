#pragma once
#include "domain/HostTypes.h"
namespace host {
// Shared by all task producers and by the monitor/cache boundary. Idempotent;
// wire confirmation and replay flags remain independent of presentation text.
void normalizeFramePresentation(FrameRecord&);
QString frameColumnKey(const FrameRecord&,int column);
}
