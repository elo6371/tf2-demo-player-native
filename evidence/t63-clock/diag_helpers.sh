#!/usr/bin/env bash
cd /d/TF2_Native_Worktrees/t63-clock || exit 1
OUT=evidence/t63-clock/gate
eval "$(sed -n '84,142p' demo-clock-check.sh)"
val=$(v "$OUT/S1-scan.txt" view_last5)
echo "raw=[$val]"
echo "last=[$(last_of_list "$val")]"
echo "first=[$(first_of_list "$val")]"
PROBE=native/build-nmake/demo_open_probe.exe
P1="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-09-10_14-04-13.dem"
echo "== P1 scan =="
"$PROBE" --scan "$P1" | tr ' ' '\n' | grep -E "camera_track|view_first5|view_last5|view_signon|view_out_of_order|view_ordered|first_data_entry|first_data_net|ticks=|packets=|source_tv|recording|setview"
echo "== P1 camera-tick 600 =="
"$PROBE" --scan --camera-tick 600 "$P1" | tr ' ' '\n' | grep -E "camera_"
echo "== P1 camera-tick 300 =="
"$PROBE" --scan --camera-tick 300 "$P1" | tr ' ' '\n' | grep -E "camera_"