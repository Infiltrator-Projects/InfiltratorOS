// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_WORKER_DEPENDENCIES_H
#define INFILTRATOROS_NATIVE_WORKER_DEPENDENCIES_H
#include "release.h"
namespace infiltrator_native::worker {
Strings application_build_dependencies(const Entry &entry);
void prepare_build_dependencies(const Entry &entry, Status &status);

} // namespace infiltrator_native::worker
#endif
