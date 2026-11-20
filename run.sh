#!/bin/sh
set -eu
/app/pipeline
exec python /app/convert.py
