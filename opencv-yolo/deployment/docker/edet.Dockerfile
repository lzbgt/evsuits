FROM python:slim

ENV MAINTAINER=Bruce.Lu
WORKDIR /apps/app
RUN apt -qq update && apt install -y redis-server

ENV BIN_PRE=/usr/local/bin/python
ENV BIN_NAME=web/detect_video.py
ENV DL_DIR=/data
#ENV REDIS=
ENV CFG_DIR=/apps/app/
ENV BIN_DIR=/apps/app/

COPY opencv-yolo/web/web.py /apps/app/
COPY opencv-yolo/web/web.py /apps/app/
COPY opencv-yolo/web/detect_video.py /apps/app/

COPY opencv-yolo/web/requirement.txt /apps/app/
RUN pip install -r requirement.txt

COPY opencv-yolo/web/start.sh /apps/app

EXPOSE 5555
EXPOSE 5000
CMD ["./start.sh"]