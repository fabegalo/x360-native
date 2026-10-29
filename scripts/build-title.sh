#!/bin/sh
# Native AOT entry point for Linux/macOS. Tools must be native to this host.
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
task_supervisor=${X360N_SUPERVISOR:-}
if [ -z "$task_supervisor" ]; then
    for task_candidate in "$task_root/x360n" "$task_root/.local/build/supervisor-only/src/supervisor/x360n"; do
        if [ -x "$task_candidate" ]; then task_supervisor=$task_candidate; break; fi
    done
fi
if [ -z "$task_supervisor" ]; then
    echo 'Native supervisor missing. Set X360N_SUPERVISOR to the built x360n executable.' >&2
    exit 3
fi
exec "$task_supervisor" --root "$task_root" prepare "$@"
