FROM alpine:latest

# gcc/musl-dev: compile the allocator
# make: build system
# clang/clang-extra-tools: clang-format and clang-tidy
# python3: test runner (runtest.py)
# gcompat: glibc compatibility shim so the pre-compiled reference binaries
#           in tests/expected/ (glibc-linked) can run on musl/Alpine
RUN apk add --no-cache \
    gcc \
    musl-dev \
    make \
    clang \
    clang-extra-tools \
    python3 \
    gcompat

WORKDIR /app
COPY . .
RUN make tests

CMD ["python3", "./runtest.py"]
