#!/bin/sh
redis-server &
celery multi start 2 -E -A web.worker -l info -n %n.%%h --autoscale=4,1 --pidfile=%n.pid
flower -A web.worker --loglevel=info &
python web.py