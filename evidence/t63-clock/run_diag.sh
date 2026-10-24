#!/usr/bin/env bash
set -uo pipefail
cd /d/TF2_Native_Worktrees/t63-clock || exit 1
mkdir -p evidence/t63-clock/probe
echo "== acquire $(date '+%F %T') =="
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh acquire t63-clock 5400
arc=$?
if [ "$arc" -ne 0 ]; then echo "ACQUIRE_FAILED rc=$arc"; exit 75; fi
trap 'bash /d/TF2_Native_Worktrees/_shared/hostlock.sh release t63-clock' EXIT
echo "== build $(date '+%T') =="
bash build-target.sh demo_open_probe
brc=$?
echo "== build rc=$brc $(date '+%T') =="
[ "$brc" -eq 0 ] || exit "$brc"
P=./native/build-nmake/demo_open_probe.exe
S1="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
S2="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/review12.dem"
S3="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/73.dem"
S4="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/prodcutscout.dem"
S5="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
X1="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/SUNSHINE.dem"
scan() { id=$1; demo=$2; echo "== scan $id $(date '+%T') =="; "$P" --scan "$demo" > "evidence/t63-clock/probe/$id.scan.txt" 2>&1; echo "scan rc=$?"; }
scan S1 "$S1"; scan S2 "$S2"; scan S3 "$S3"; scan S4 "$S4"; scan S5 "$S5"; scan X1 "$X1"
echo "== snapshots $(date '+%T') =="
"$P" --scan --snapshot 602 "$S1" > evidence/t63-clock/probe/S1.snap-entry602.txt 2>&1
"$P" --scan --snapshot 10919 "$S1" > evidence/t63-clock/probe/S1.snap-server10919.txt 2>&1
"$P" --scan --snapshot 601 "$S5" > evidence/t63-clock/probe/S5.snap-entry601.txt 2>&1
"$P" --scan --snapshot 52411 "$S5" > evidence/t63-clock/probe/S5.snap-server52411.txt 2>&1
echo "== done $(date '+%T') =="
