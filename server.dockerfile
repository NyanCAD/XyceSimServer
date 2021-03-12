FROM xyce:latest AS build

COPY . /tmp/build
WORKDIR /tmp/build

RUN mkdir -p build; \
cd build; \
cmake -DCMAKE_INSTALL_PREFIX="/usr/local" ..; \
make DESTDIR=/tmp -j$(nproc) install

FROM xyce:latest

COPY --from=build /tmp/usr/local /usr/local

EXPOSE 5923
CMD ["XyceSimServer"]