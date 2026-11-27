// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_WORKER_INSTALL_H
#define INFILTRATOROS_NATIVE_WORKER_INSTALL_H
#include "release.h"
namespace infiltrator_native::worker {
int smart_queue(const std::vector<Entry> &entries);
int individual(const Entry &entry);

} // namespace infiltrator_native::worker
#endif
