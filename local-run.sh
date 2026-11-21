#!/bin/sh
set -eu
cd "$(dirname "$0")"
exec "${PIPELINE_BIN:-./pipeline}" "$@"
