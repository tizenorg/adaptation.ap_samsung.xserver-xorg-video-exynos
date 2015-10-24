#!/bin/bash

# restart X server without Enlightenment
echo -n "restart X server..."

pkill X
startx --only 2> /dev/null
sleep 2

echo " ok."

echo -n "close all windows..."

# sometimes, after startx --only, we have few strange windows,
# we must close them to proper work of hwc-sample and square_bubbles
square_bubbles -cls > /dev/null

echo " ok."

echo "start apps:"

snowflake -geo 200x200 100 100&
snowflake -geo 200x200 200 200&
snowflake -geo 200x200 500 500&

echo " snowflake -geo 200x200 100 100"
echo " snowflake -geo 200x200 200 200"
echo " snowflake -geo 200x200 500 500"

sleep 1

echo -n "start square_bubbles..."
square_bubbles -m -s > /dev/null &
echo " ok."

echo " sleep 15 sec"
sleep 15

echo -n "start hwc_sample..."
hwc_sample -redir -setr > /dev/null &
echo " ok."

echo " sleep 15 sec"
sleep 15

pkill square_bubbles
pkill hwc_sample
pkill snowflake
