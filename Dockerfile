FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends g++ make python3 numactl ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /opt/mars
COPY . .
RUN python3 scripts/build.py && python3 -m unittest discover -s tests -v && build/bin/mars_accept unit build/toy_acceptance
ENV OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
CMD ["python3", "scripts/reproduce.py", "--help"]
