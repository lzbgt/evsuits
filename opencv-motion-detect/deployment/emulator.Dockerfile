FROM jrottenberg/ffmpeg:4.1-alpine

RUN apk update && apk --no-cache add tini curl
COPY run.sh /bin/run.sh
RUN chmod +x /bin/run.sh
STOPSIGNAL SIGINT

ENTRYPOINT [ "tini", "-v", "--", "/bin/run.sh" ]
