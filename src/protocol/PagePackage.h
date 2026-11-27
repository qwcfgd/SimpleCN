#pragma once
#include "FirmwareImage.h"

namespace boot::page {
// Inspect file bytes before mapping binary members to the configured destination.
bool resembles(const FirmwareImage &image);
bool inspect(const FirmwareImage &image, quint32 &kind, QVector<Segment> &members, QString &error);
}
