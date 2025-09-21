# ===================================================================
# STAGE 1: Build Phase (Builder)
# ===================================================================
FROM ubuntu:22.04 AS builder

# Set working directory
WORKDIR /app

# Update package list and install dependencies required for building
# g++: C++ compiler
# make: convenient build tool (though we directly use g++ here)
# libpcre2-dev: development files for the PCRE2 regex library, required for compilation
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    g++ \
    make \
    libpcre2-dev && \
    rm -rf /var/lib/apt/lists/*

# Copy source code into the container
COPY compressor.cpp .

# Run compilation command to generate the executable Delog_compress
# -std=c++17: use C++17 standard
# -O3: enable maximum optimization
# -lpcre2-8 -lstdc++fs -pthread: link required libraries
RUN g++ -std=c++17 -O3 -o Delog_compress compressor.cpp -lpcre2-8 -lstdc++fs -pthread

# ===================================================================
# STAGE 2: Final Runtime Phase (Final Image)
# ===================================================================
FROM ubuntu:22.04

# Set working directory
WORKDIR /app

# Update package list and install runtime dependencies
# libpcre2-8-0: runtime library for PCRE2
# tar: called via system() in C++ code
# xz-utils, gzip, bzip2, lz4: tools for different compression formats used with tar
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    libpcre2-8-0 \
    tar \
    xz-utils \
    gzip \
    bzip2 \
    lz4 && \
    rm -rf /var/lib/apt/lists/*

# Copy the compiled executable from the 'builder' stage into this stage
COPY --from=builder /app/Delog_compress .

# Copy entrypoint script and give it execution permission
COPY entrypoint.sh .
RUN chmod +x entrypoint.sh

# Create directories for mounting data volumes
RUN mkdir -p /data /output

# Set the container entrypoint
ENTRYPOINT ["/app/entrypoint.sh"]

# Set default command, shows help message when no arguments are passed
CMD ["--help"]
