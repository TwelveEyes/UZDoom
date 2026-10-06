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
# A run is PASS only if: exit 255 (normal timedemo end), the perflog
# exists and contains the "F 2600" summary line, and stdout carries the
# backend-selection line matching the requested backend. Exit 255 alone
# proves nothing about the log (r_perflog fails silently if the path is
# not writable — e.g. /tmp under a sandboxed shell).
#
# Usage:
#   run_matrix.sh                 # full 52-run matrix, sequential
#   run_matrix.sh --only HEXEN    # only demos whose name matches HEXEN
#   run_matrix.sh --only HEXEN --backend vulkan
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

while [ $# -gt 0 ]; do
	case "$1" in
	--only) ONLY="$2"; shift 2 ;;
	--backend) BACKEND_FILTER="$2"; shift 2 ;;
	*) echo "unknown arg: $1" >&2; exit 2 ;;
	esac
done

if [ ! -x "$BUILD/uzdoom" ]; then
	echo "error: $BUILD/uzdoom missing or not executable (build first)" >&2
	exit 2
fi

mkdir -p "$LOGS"
STATUS="$LOGS/status.tsv"
printf 'demo\tbackend\texit\tverdict\twall_s\tlline\n' > "$STATUS"

# demo name -> iwad file under build/wads/
iwad_for() {
	case "$1" in
	DOOM2_*)   echo DOOM2.WAD ;;
	HERETIC_*) echo HERETIC.WAD ;;
	HEXEN_*)   echo HEXEN.WAD ;;
	MYHOUSE_*) echo myhouse.pk3 ;;
	PIRATES_*) echo "Pirates!.wad" ;;
	PLANISF_*) echo planisf2.wad ;;
	SOS_*)     echo SOS_Boom.wad ;;
	*) echo "" ;;
	esac
}

# collect demo list
DEMOS=$(ls "$KIT/demos" | sed 's/\.lmp$//' | sort)

TOTAL=0
PASS=0
FAIL=0
for demo in $DEMOS; do
	for backend in gl33 vulkan; do
		if [ -n "$ONLY" ]; then
			case "$demo" in *"$ONLY"*) ;; *) continue ;; esac
		fi
		if [ -n "$BACKEND_FILTER" ] && [ "$backend" != "$BACKEND_FILTER" ]; then
			continue
		fi
		TOTAL=$((TOTAL+1))
		case "$backend" in
			gl33) N=0; WANT='Selecting OpenGL backend' ;;
			vulkan) N=1; WANT='Selecting Vulkan backend' ;;
		esac
		IWAD=$(iwad_for "$demo")
		if [ -z "$IWAD" ]; then
			printf '%s\t%s\t-\tFAIL\t0\tunknown iwad prefix\n' "$demo" "$backend" >> "$STATUS"
			FAIL=$((FAIL+1))
			echo "[$TOTAL] $demo $backend FAIL (unknown iwad prefix)"
			continue
		fi
		[ -f "$BUILD/wads/$IWAD" ] || {
			printf '%s\t%s\t-\tFAIL\t0\tmissing wads/%s\n' "$demo" "$backend" "$IWAD" >> "$STATUS"
			FAIL=$((FAIL+1))
			echo "[$TOTAL] $demo $backend FAIL (missing wads/$IWAD)"
			continue
		}
		[ -f "$KIT/demos/$demo.lmp" ] || {
			printf '%s\t%s\t-\tFAIL\t0\tmissing demo\n' "$demo" "$backend" >> "$STATUS"
			FAIL=$((FAIL+1))
			echo "[$TOTAL] $demo $backend FAIL (missing demos/$demo.lmp)"
			continue
		}

		PLOG="$LOGS/${demo}_${backend}.perflog"
		OUT="$LOGS/${demo}_${backend}.out"
		CFG="$LOGS/.cfg_${demo}_${backend}.cfg"
		rm -f "$PLOG"
		printf 'r_perflog %s\n' "$PLOG" > "$CFG"

		t0=$(date +%s)
		(
			cd "$BUILD"
			"$BUILD/uzdoom" \
				-iwad "wads/$IWAD" \
				-nomonsters \
				-width "$W" -height "$H" \
				-exec "$KIT/baseline.cfg" \
				+set vid_preferbackend "$N" \
				+set r_perflog "$PLOG" \
				-timedemo "$KIT/demos/$demo.lmp" \
				> "$OUT" 2>&1
		)
		rc=$?
		t1=$(date +%s)
		wall=$((t1-t0))

		verdict=PASS
		detail=""
		[ "$rc" -ne 255 ] && { verdict=FAIL; detail="exit $rc"; }
		if [ "$verdict" = PASS ]; then
			if [ ! -s "$PLOG" ]; then
				verdict=FAIL; detail="perflog missing/empty"
			elif ! grep -q '^F 2600' "$PLOG"; then
				verdict=FAIL; detail="no F 2600 summary line"
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
