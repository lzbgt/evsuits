#!/bin/sh
redis-server &
celery multi start 4 -E -A web.worker -l info --autoscale=4,1 --pidfile=%n.pid
flower -A web.worker --loglevel=info &
python web.py