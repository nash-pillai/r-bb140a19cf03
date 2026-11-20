import os
from pathlib import Path

import polars as pl

output = Path(os.environ.get("OUTPUT_DIR", "/output"))
for table in ("user_daily", "campaign_daily"):
    folder = output / table
    schema = {"day": pl.Int32, "sessions": pl.Int64, "revenue_cents": pl.Int64}
    if table == "user_daily":
        schema.update(user_id=pl.String, events=pl.Int64, top_category=pl.String)
    else:
        schema.update(
            campaign_id=pl.String,
            channel=pl.String,
            purchasing_sessions=pl.Int64,
            assisted_sessions=pl.Int64,
        )
    raw = folder / "raw.csv"
    rows = pl.scan_csv(raw, schema_overrides=schema).with_columns(pl.col("day").cast(pl.Date))
    rows.sink_parquet(folder / "part.parquet", compression="uncompressed")
    raw.unlink()
