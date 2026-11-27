// SPDX-License-Identifier: GPL-3.0-or-later
// The installed launcher is deliberately only an entry point.
#include "native-worker/dispatch.h"

int main(int argc, char **argv) {
    return infiltrator_native::worker::native_component_worker_entrypoint(argc, argv);
}
