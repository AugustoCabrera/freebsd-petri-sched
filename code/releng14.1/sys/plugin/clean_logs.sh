#!/bin/sh

# Limpia los logs locales usados por los módulos
echo "Limpiando /var/log/local0.log y /var/log/local2.log..."

: > /var/log/local0.log
: > /var/log/local1.log
: > /var/log/local2.log

# Reinicia el demonio de syslog para evitar buffers colgados
service syslogd restart

echo "Hecho."
