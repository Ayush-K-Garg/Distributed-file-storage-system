FROM gcc:latest
WORKDIR /usr/src/app
COPY metadata_server/ ./metadata_server/
COPY common/ ./common/
RUN g++ -O3 metadata_server/MetadataServer.cpp -o meta_server -lpthread
EXPOSE 8001
CMD ["./meta_server"]