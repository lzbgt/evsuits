FROM centos:7
ENV __MAINTAINER_=Bruce.LU
ENV __EMAIL__=lzbgt@icloud.com
ENV TZ=UTC
ENV LD_LIBRARY_PATH=/apps/lib:/apps/lib64
WORKDIR /apps
RUN curl -OL https://github.com/lzbgt/opencv-pocs/releases/download/v0.9.9-1/evcloudsvc && \
curl -OL https://github.com/lzbgt/opencv-pocs/releases/download/v0.9.9-1/docker-centos7_libs.tar && \
curl -OL https://github.com/lzbgt/opencv-pocs/releases/download/v0.9.9-1/libstdcxx-centos7.tar && \
chmod +x ./evcloudsvc && tar xf docker-centos7_libs.tar --strip-components=1 -C ./ && \
tar xf libstdcxx-centos7.tar --strip-components=1 -C ./lib64 && \
rm -fr *.tar

CMD ["/apps/evcloudsvc"]