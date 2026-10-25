#!/usr/bin/env bash
# Boot an emulated panel against the fake manager, and use it from a browser.
#
#   qemu/live.sh                        # prints the viewer's URL; Ctrl-C stops it
#   qemu/live.sh --http 8080 --rotation 90 --weather
#
# Takes the same environment as run_tests.sh (NSPM_QEMU_SKIP_BUILD=1 to skip the build,
# NEXTION_TOOLS=<checkout>, ...). Logs and screenshots go to qemu/build/live/.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "$QEMU_DIR"
exec "$VENV/bin/python" -m nspm_qemu.live "$@"
