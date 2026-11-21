FROM scratch AS wheel
# The amd64 PyArrow wheel supplies the matching Arrow/Parquet C++ headers and libraries.
ADD --checksum=sha256:4250e28a22302ce8692d3a0e8ec9d9dde54ec00d237cff4dfa9c1fbf79e472a8 https://files.pythonhosted.org/packages/bb/1b/2168d6050e52ff1e6cefc61d600723870bf569cbf41d13db939c8cf97a16/pyarrow-20.0.0-cp312-cp312-manylinux_2_28_x86_64.whl /wheel.zip

FROM python:3.12.8-slim-bookworm@sha256:2199a62885a12290dc9c5be3ca0681d367576ab7bf037da120e564723292a2f0 AS arrow
COPY --from=wheel /wheel.zip /wheel.zip
RUN python -m zipfile -e /wheel.zip /wheel

FROM gcc:14.2-bookworm@sha256:b99b86a28812b1e6453a231a947dc43d76fe192788a12f344a9b568bf9f5d24c AS build
WORKDIR /build
COPY --from=arrow /wheel/pyarrow/include /opt/arrow/include
COPY --from=arrow /wheel/pyarrow/libarrow.so.2000 /wheel/pyarrow/libparquet.so.2000 /opt/arrow/
COPY main.cpp daily_writer.h ./
RUN g++ -O3 -DNDEBUG -std=c++20 -pthread -I/opt/arrow/include main.cpp -L/opt/arrow -Wl,-rpath,'$ORIGIN/lib' -l:libparquet.so.2000 -l:libarrow.so.2000 -o pipeline && cp -L "$(g++ -print-file-name=libstdc++.so.6)" libstdc++.so.6

FROM python:3.12.8-slim-bookworm@sha256:2199a62885a12290dc9c5be3ca0681d367576ab7bf037da120e564723292a2f0
WORKDIR /app
COPY --from=build /opt/arrow/lib*.so.2000 /build/libstdc++.so.6 /app/lib/
COPY --from=build /build/pipeline /app/
COPY --chmod=755 run.sh /app/
ENV HOME=/scratch PYTHONDONTWRITEBYTECODE=1
USER 1000:1000
CMD ["/app/run.sh"]
