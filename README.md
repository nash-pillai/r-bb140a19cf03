# Shopping Sessions

An exact native pipeline for the Builders Buildathon Shopping Sessions track.

The C++ core canonicalizes linked identities, parses event files in parallel, and partitions records by exact event ID. It selects the earliest ingested copy before dropping invalid events, then sorts surviving events into sessions and maintains a seven-day window of advertising clicks. The Python conversion step writes both required Parquet tables with a DATE day column.

## Local checks

Create a Python virtual environment and install the pinned Polars dependency. Compile the core with a C++20 compiler.

```sh
python -m venv .venv
.venv/bin/pip install polars==1.37.1
g++ -O3 -DNDEBUG -std=c++20 -o pipeline main.cpp
.venv/bin/python scripts/run_fixtures.py fixtures -- ./local-run.sh
```

Set DATA_DIR and OUTPUT_DIR to run on generated data. The container uses /data and /output by default.

The Docker image exactly matched the official answers for one million events. On kevin-old, the parallel C++ core processed 20 million events and five million clicks in 55.5 seconds. Both tables matched the serial implementation. That measurement excludes Parquet conversion.

## Attribution

The scripts and fixtures originated from the organizers' official DuckDB starter, downloaded November 20, 2026. The native computation and conversion implementations were written during the event. The root Dockerfile provides the scorer entrypoint.
