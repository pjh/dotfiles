#!/bin/bash

#https://help.ubuntu.com/community/Logitech_Marblemouse_USB

dev="Logitech USB Trackball"
we="Evdev Wheel Emulation"
# Right-handed:
#xinput set-int-prop "$dev" "$we Button" 8 8
# Left-handed:
xinput set-int-prop "$dev" "$we Button" 8 9 > /dev/null 2>&1
# Not sure what this does:
#xinput set-int-prop "$dev" "$we" 8 1
xinput set-int-prop "$dev" "$we" 8 3 > /dev/null 2>&1

# xinput set-int-prop "$dev" "$we" 8 1
# xinput set-int-prop "$dev" "$we Button" 8 9
# xinput set-int-prop "$dev" "$we X Axis" 8 6 7
# xinput set-int-prop "$dev" "$we Y Axis" 8 4 5
# xinput set-int-prop "$dev" "Drag Lock Buttons" 8 8 
