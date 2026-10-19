#!/usr/bin/env bash
# run_matrix.sh — ticket 10 phase-3 driver.
#
# Runs the classic-path baseline matrix: every demo in demos/ on both
# backends (vid_preferbackend 0=GL 3.3, 1=Vulkan) at the host-native
# resolution 2560x1440 (the reference machine's DP-1 panel; the resolution
# axis was dropped 2026-10-05 — see manifest.md "Resolutions").
#
# Per run:
#   - perflog  tools/abtest/logs/<demo>_<backend>.perflog  (r_perflog)
#   - stdout   tools/abtest/logs/<demo>_<backend>.out
#   - config   tools/abtest/logs/.cfg_<demo>_<backend>.cfg
# A run is PASS only if: exit 255 (normal timedemo end, auto-detected by
# wait_timedemo below — the engine is terminated at the end dialog), the perflog
# exists and shows the demo played through (last F-line tic >= 2599), and
# stdout carries the backend-selection line matching the requested
# backend. Notes: timedemos play back at the game tic rate (~35 tics/s)
# while rendering runs uncapped, so each run takes ~80s regardless of map
# size and the perflog holds 40k-150k per-frame lines; "F 2600" is just
# frame #2600 mid-file, NOT a completion marker (grep '^F 2600' also
# matches F 2600x and is a false-positive trap). Exit 255 alone proves
# nothing about the log (r_perflog fails silently if the path is not
# writable — e.g. /tmp under a sandboxed shell).
#
# Usage:
#   run_matrix.sh                 # full 52-run matrix, sequential
#   run_matrix.sh --only HEXEN    # only demos whose name matches HEXEN
#   run_matrix.sh --only A --only B   # OR-match several name patterns
#   run_matrix.sh --only HEXEN --backend vulkan
#   run_matrix.sh --demos demos/v2 --tag v2        # perf baseline v2 tour set
#   run_matrix.sh --demos demos/vantage --tag vant --tics 1050
#   run_matrix.sh --demos demos/v2 --tag mon --monsters --only SOS
# --demos  demo directory (default demos/); --tag  log-name prefix;
# --monsters  drop -nomonsters; --tics  demo length for the completion check;
# --set 'CVAR VALUE'  extra +set on the engine command line (repeatable).
# Every run loads baseline.cfg (incl. r_ticstability 0, perf baseline v2).
# Status: tools/abtest/logs/status.tsv (demo, backend, exit, verdict,
# wall_s, lline).

set -u

ROOT=$(cd "$(dirname "$0")/../.." && pwd)       # repo root (script lives in tools/abtest/)
BUILD="$ROOT/build"
KIT="$ROOT/tools/abtest"
LOGS="$KIT/logs"
W=2560
H=1440
ONLY=""
BACKEND_FILTER=""
DEMODIR="$KIT/demos"
TAG=""
MONSTERS=0
TICS=2600
SETS=()

while [ $# -gt 0 ]; do
	case "$1" in
	--only) ONLY="$ONLY$2 "; shift 2 ;;
	--backend) BACKEND_FILTER="$2"; shift 2 ;;
	--demos) DEMODIR=$(cd "$2" && pwd); shift 2 ;;
	--tag) TAG="$2_"; shift 2 ;;
	--monsters) MONSTERS=1; shift ;;
	--tics) TICS="$2"; shift 2 ;;
	--set) SETS+=(+set ${2%% *} "${2#* }"); shift 2 ;;
	*) echo "unknown arg: $1" >&2; exit 2 ;;
	esac
done

if [ ! -x "$BUILD/uzdoom" ]; then
	echo "error: $BUILD/uzdoom missing or not executable (build first)" >&2
	exit 2
fi

mkdir -p "$LOGS"
STATUS="$LOGS/${TAG}status.tsv"
printf 'demo\tbackend\texit\tverdict\twall_s\tlline\n' > "$STATUS"

# demo name -> base IWAD under build/wads/. The three commercial IWADs
# (DOOM2/HEXEN/HERETIC) run standalone; the four non-commercial WADs are
# PWADs that must be mounted on top of the DOOM2 base (-iwad DOOM2.WAD
# -file <pwad>) — launched as a bare -iwad the engine aborts before the
# demo loads. Sets BASE_IWAD and PWAD_FILE (empty for standalone IWADs).
mount_for() {
	BASE_IWAD=""
	PWAD_FILE=""
	case "$1" in
	DOOM2_*)   BASE_IWAD=DOOM2.WAD ;;
	HERETIC_*) BASE_IWAD=HERETIC.WAD ;;
	HEXEN_*)   BASE_IWAD=HEXEN.WAD ;;
	MYHOUSE_*) BASE_IWAD=DOOM2.WAD; PWAD_FILE=myhouse.pk3 ;;
	PIRATES_*) BASE_IWAD=DOOM2.WAD; PWAD_FILE="Pirates!.wad" ;;
	PLANISF_*) BASE_IWAD=DOOM2.WAD; PWAD_FILE=planisf2.wad ;;
	SOS_*)     BASE_IWAD=DOOM2.WAD; PWAD_FILE=SOS_Boom.wad ;;
	GATHERTEST_*) BASE_IWAD=DOOM2.WAD; PWAD_FILE=gathertest.wad ;;
	OOBTEST_*) BASE_IWAD=DOOM2.WAD; PWAD_FILE=oobtest.wad ;;
	*) ;;
	esac
}

# A timedemo ends with I_FatalError("timed N gametics ..."), which opens the
# error window and blocks until a user closes it (the message never reaches
# stdout). Detect the end from the perflog instead: once the last F line has
# reached tic TICS-1 and the log has stopped growing for 3 s, the demo is
# over and the engine is parked in that dialog, so terminate it. Returns 255
# for a finished demo (the code the engine exits with when the dialog is
# closed), 124 on the TIMEOUT backstop, else the engine's own exit code.
TIMEOUT=600
wait_timedemo() {
	local pid=$1 plog=$2 start last size prev=-1 still=0
	start=$(date +%s)
	while kill -0 "$pid" 2>/dev/null; do
		sleep 1
		size=$(stat -c %s "$plog" 2>/dev/null || echo 0)
		if [ "$size" = "$prev" ]; then still=$((still+1)); else still=0; prev=$size; fi
		if [ "$still" -ge 3 ]; then
			last=$(awk '$1=="F"{t=$3} END{sub("tic=","",t); print t+0}' "$plog" 2>/dev/null)
			if [ "${last:-0}" -ge $((TICS-1)) ]; then
				kill "$pid" 2>/dev/null; sleep 2; kill -9 "$pid" 2>/dev/null
				wait "$pid" 2>/dev/null
				return 255
			fi
		fi
		if [ $(( $(date +%s) - start )) -ge "$TIMEOUT" ]; then
			kill -9 "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
			return 124
		fi
	done
	wait "$pid"
}

# collect demo list
DEMOS=$(ls "$DEMODIR" | grep '\.lmp$' | sed 's/\.lmp$//' | sort)

TOTAL=0
PASS=0
FAIL=0
for demo in $DEMOS; do
	for backend in gl33 vulkan; do
		if [ -n "$ONLY" ]; then
			match=0
			for pat in $ONLY; do
				case "$demo" in *"$pat"*) match=1; break ;; esac
			done
			[ "$match" -eq 1 ] || continue
		fi
		if [ -n "$BACKEND_FILTER" ] && [ "$backend" != "$BACKEND_FILTER" ]; then
			continue
		fi
		TOTAL=$((TOTAL+1))
		case "$backend" in
			gl33) N=0; WANT='Selecting OpenGL backend' ;;
			vulkan) N=1; WANT='Selecting Vulkan backend' ;;
		esac
		mount_for "$demo"
		if [ -z "$BASE_IWAD" ]; then
			printf '%s\t%s\t-\tFAIL\t0\tunknown iwad prefix\n' "$demo" "$backend" >> "$STATUS"
			FAIL=$((FAIL+1))
			echo "[$TOTAL] $demo $backend FAIL (unknown iwad prefix)"
			continue
		fi
		[ -f "$BUILD/wads/$BASE_IWAD" ] || {
			printf '%s\t%s\t-\tFAIL\t0\tmissing wads/%s\n' "$demo" "$backend" "$BASE_IWAD" >> "$STATUS"
			FAIL=$((FAIL+1))
			echo "[$TOTAL] $demo $backend FAIL (missing wads/$BASE_IWAD)"
			continue
		}
		if [ -n "$PWAD_FILE" ] && [ ! -f "$BUILD/wads/$PWAD_FILE" ]; then
			printf '%s\t%s\t-\tFAIL\t0\tmissing wads/%s\n' "$demo" "$backend" "$PWAD_FILE" >> "$STATUS"
			FAIL=$((FAIL+1))
			echo "[$TOTAL] $demo $backend FAIL (missing wads/$PWAD_FILE)"
			continue
		fi
		[ -f "$DEMODIR/$demo.lmp" ] || {
			printf '%s\t%s\t-\tFAIL\t0\tmissing demo\n' "$demo" "$backend" >> "$STATUS"
			FAIL=$((FAIL+1))
			echo "[$TOTAL] $demo $backend FAIL (missing demos/$demo.lmp)"
			continue
		}

		PLOG="$LOGS/${TAG}${demo}_${backend}.perflog"
		OUT="$LOGS/${TAG}${demo}_${backend}.out"
		CFG="$LOGS/.cfg_${TAG}${demo}_${backend}.cfg"
		rm -f "$PLOG"
		printf 'r_perflog %s\n' "$PLOG" > "$CFG"

		t0=$(date +%s)
		EXTRA=()
		if [ -n "$PWAD_FILE" ]; then
			EXTRA=(-file "wads/$PWAD_FILE")
		fi
		if [ "$MONSTERS" -eq 0 ]; then
			EXTRA+=(-nomonsters)
		fi
		(
			cd "$BUILD"
			# line-buffered: the engine is killed at the end dialog, and a
			# block-buffered stdout would be lost with it
			exec stdbuf -oL -eL "$BUILD/uzdoom" \
				-iwad "wads/$BASE_IWAD" \
				"${EXTRA[@]}" \
				-width "$W" -height "$H" \
				-exec "$KIT/baseline.cfg" \
				+set vid_preferbackend "$N" \
				+set r_perflog "$PLOG" \
				"${SETS[@]}" \
				-timedemo "$DEMODIR/$demo.lmp" \
				> "$OUT" 2>&1
		) &
		pid=$!
		wait_timedemo "$pid" "$PLOG"
		rc=$?
		t1=$(date +%s)
		wall=$((t1-t0))

		verdict=PASS
		detail=""
		[ "$rc" -ne 255 ] && { verdict=FAIL; detail="exit $rc"; }
		if [ "$verdict" = PASS ]; then
			if [ ! -s "$PLOG" ]; then
				verdict=FAIL; detail="perflog missing/empty"
			else
				# completion check: the demo's last F-line must have reached
				# tic TICS-1 (demo is TICS tics from level start)
				LASTTIC=$(awk '$1=="F"{t=$3} END{sub("tic=","",t); print t+0}' "$PLOG")
				if [ "${LASTTIC:-0}" -lt $((TICS-1)) ]; then
					verdict=FAIL; detail="demo not fully played (last tic $LASTTIC < $((TICS-1)))"
				fi
			fi
		fi
		if [ "$verdict" = PASS ] && ! grep -qF "$WANT" "$OUT"; then
			verdict=FAIL; detail="stdout lacks '$WANT'"
		fi
		# GPU reality checks: a sandboxed shell blocks /dev/dri, and the engine
		# then silently renders with llvmpipe (software GL) — even the Vulkan
		# backend, which falls back to GL when no ICD is found. Such numbers
		# are useless for the baseline; reject the run instead of recording it.
		if [ "$verdict" = PASS ] && grep -q 'llvmpipe' "$OUT"; then
			verdict=FAIL; detail="llvmpipe in stdout (software GL — run unsandboxed)"
		fi
		if [ "$backend" = vulkan ] && [ "$verdict" = PASS ] && grep -q 'Initialization of Vulkan failed' "$OUT"; then
			verdict=FAIL; detail="vulkan init failed (fell back to GL)"
		fi
		LLINE=$(grep -m1 '^L ' "$PLOG" 2>/dev/null || echo "")

		if [ "$verdict" = PASS ]; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
		[ "$verdict" = FAIL ] && detail="${detail:-} ${LLINE}"
		printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$demo" "$backend" "$rc" "$verdict" "$wall" "$LLINE" >> "$STATUS"
		echo "[$TOTAL] $demo $backend -> $verdict (exit $rc, ${wall}s) ${LLINE}${detail:+  $detail}"
	done
done

echo "----"
echo "matrix done: $((PASS+FAIL)) runs, $PASS pass, $FAIL fail (status: $STATUS)"
[ "$FAIL" -eq 0 ]
