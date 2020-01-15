FROM python:slim

ENV MAINTAINER=Bruce.Lu
WORKDIR /apps/app
RUN apt -qq update && apt install -y redis-server build-essential

ENV BIN_PRE=/usr/local/bin/python
ENV BIN_NAME=detect_video.py
ENV DL_DIR=/data
#ENV REDIS=
ENV BIN_DIR=/apps/app/
ENV CFG_DIR=edet_model.pth 

COPY requirement.txt /apps/app/

RUN pip install --upgrade cython numpy
RUN pip install pillow==6.1 torchvision opencv-contrib-python
RUN pip install -r requirement.txt
COPY web.py /apps/app/
COPY detect_video.py /apps/app/
COPY edet_model.pth /apps/app/
COPY src  /apps/app/src
COPY start.sh /apps/app

EXPOSE 5555
EXPOSE 5000
CMD ["./start.sh"]