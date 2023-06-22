FROM debian:bullseye-slim as hotstuff
RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get -y install git gcc g++ make cmake libuv1-dev libssl-dev libsodium-dev autoconf libnet1-dev libtool pastebinit python3 bash gdb dnsutils nano inetutils-ping net-tools sudo iproute2
RUN git clone https://github.com/aixoss/gmp && cd gmp && ./configure && make install
RUN git clone https://github.com/hot-stuff/libhotstuff.git && cd libhotstuff && git submodule update --init --recursive && git submodule update --recursive --remote
RUN cd libhotstuff && cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED=ON -DHOTSTUFF_PROTO_LOG=ON && make

ADD ./scripts /
COPY ./scripts /libhotstuff/examples
COPY ./hotstuff.conf /libhotstuff
ENTRYPOINT ["/run_demo.sh"]


FROM debian:bullseye-slim as hotstuff_client
RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get -y install git gcc g++ make cmake libuv1-dev libssl-dev libsodium-dev autoconf libnet1-dev libtool pastebinit python3 bash gdb dnsutils nano inetutils-ping net-tools sudo iproute2
RUN git clone https://github.com/aixoss/gmp && cd gmp && ./configure && make install
RUN git clone https://github.com/hot-stuff/libhotstuff.git && cd libhotstuff && git submodule update --init --recursive && git submodule update --recursive --remote
RUN cd libhotstuff && cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED=ON -DHOTSTUFF_PROTO_LOG=ON && make

ADD ./scripts /
COPY ./scripts /libhotstuff/examples
COPY ./hotstuff.conf /libhotstuff
ENTRYPOINT ["/run_demo_client.sh"]
