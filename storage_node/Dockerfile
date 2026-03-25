FROM gcc:latest
WORKDIR /usr/src/app

COPY storage_node/ ./storage_node/
COPY common/ ./common/

RUN g++ -O3 storage_node/StorageServer.cpp -o node_app -lpthread

CMD ["./node_app"]