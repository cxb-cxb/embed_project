#!/bin/sh
set -eu

gst-launch-1.0 -v \
  v4l2src device=/dev/video-camera0 ! \
  video/x-raw,width=640,height=480,format=NV12,framerate=30/1 ! \
  kmssink driver-name=rockchip connector-id=154 plane-id=89 force-modesetting=true fullscreen=true sync=false
