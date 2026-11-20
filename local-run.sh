#!/bin/sh
set -eu
cd "$(dirname "$0")"
"${PIPELINE_BIN:-./pipeline}"
exec .venv/bin/python convert.py
