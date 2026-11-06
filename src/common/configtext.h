#pragma once

#include "request.h"

#include <QByteArray>

// The exact text the helper writes, shared with the planner so the review screen
// shows the same bytes that will land on disk.

// /etc/default/grub.d/zz-ekpo.cfg for a non-original GRUB change.
QByteArray grubDropInText(const Request::Grub &grub);

// plymouthd.conf with [Daemon] Theme= set and every other line kept.
QByteArray withDaemonTheme(const QByteArray &conf, const QString &theme);
