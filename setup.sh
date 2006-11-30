#!/bin/sh

if [ $UID = "0" ]; then 
  echo "user root, system-wide installation assumed."
  echo
  CONF_DIR=/etc
  CACHE_DIR=/var/cache/mpdscribble
  LOG_DIR=/var/log
else
  echo "local installation."
  echo "login as root for system-wide installation."
echo 
  CONF_DIR=$HOME/.mpdscribble
  CACHE_DIR=$CONF_DIR
  LOG_DIR=$CONF_DIR
fi

LOGIN=$CONF_DIR/mpdscribble.conf
CACHE=$CACHE_DIR/mpdscribble.cache
LOG=$LOG_DIR/mpdscribble.log

echo "files will be created in these locations: "
echo "configuration file:  $LOGIN"
echo "song submit cache:   $CACHE"
echo "log file:            $LOG"
echo 
echo "press ctrl-c to cancel if this is not intended."
echo "continue..."
read -s

mkdir -p $CONF_DIR
mkdir -p $CACHE_DIR
mkdir -p $LOG_DIR

echo -n "Please enter your audioscrobbler username: "
read -e USERNAME
echo -n "and password: "
read -s -e PASSWORD

echo "username = $USERNAME" > $LOGIN
chmod 600 $LOGIN
MD5=`echo -n $PASSWORD | md5sum | awk '{print $1}'`
echo "password = $MD5" >> $LOGIN

echo "cache = $CACHE" >> $LOGIN
echo "log = $LOG" >> $LOGIN
echo "verbose = 2" >> $LOGIN

echo ""
echo "Thank you."
echo "You can try running mpdscribble now."
