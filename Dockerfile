FROM gcc:14.2-bookworm AS build
WORKDIR /build
COPY main.cpp .
RUN g++ -static-libstdc++ -static-libgcc -O3 -DNDEBUG -std=c++20 -pthread -o pipeline main.cpp
FROM python:3.12.8-slim-bookworm
RUN pip install --no-cache-dir polars==1.37.1 && useradd --uid 1000 --no-create-home --home-dir /scratch app
COPY --from=build /build/pipeline /app/pipeline
COPY convert.py run.sh /app/
RUN chmod +x /app/run.sh
ENV PYTHONDONTWRITEBYTECODE=1 POLARS_MAX_THREADS=8
WORKDIR /app
CMD ["/app/run.sh"]
