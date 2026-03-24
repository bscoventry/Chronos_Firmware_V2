#!/usr/bin/env bash
# Find which object file references __device_dts_ord_<N> (undefined symbol).
# Run after a failed link. Usage:
#   ./scripts/find_device_ord_ref.sh [ordinal] [build_dir]
# Example:
#   ./scripts/find_device_ord_ref.sh 123
#   ./scripts/find_device_ord_ref.sh 123 build/Chronos_nRF54_2
set -e
ORD="${1:-123}"
BUILD="${2:-build}"
if [ -n "$3" ]; then
  BUILD="$2/$3"
fi
# Common NCS build layout
if [ ! -d "$BUILD" ]; then
  for d in build "build/Chronos_nRF54_2" "build/nrf" "build/app"; do
    if [ -d "$d" ]; then BUILD="$d"; break; fi
  done
fi
SYM="__device_dts_ord_${ORD}"
echo "Looking for undefined reference to: $SYM"
echo "Searching in: $BUILD"
echo ""
found=0
NM=
for cmd in nm llvm-nm arm-zephyr-eabi-nm arm-none-eabi-nm; do
  if command -v $cmd >/dev/null 2>&1; then NM=$cmd; break; fi
done
if [ -n "$NM" ]; then
  while IFS= read -r -d '' f; do
    if $NM -u "$f" 2>/dev/null | grep -q "$SYM"; then
      echo "$f"
      found=1
    fi
  done < <(find "$BUILD" -name "*.o" -print0 2>/dev/null)
  # Also check .a archives (object files are inside)
  while IFS= read -r -d '' f; do
    if $NM -u "$f" 2>/dev/null | grep -q "$SYM"; then
      echo "$f"
      found=1
    fi
  done < <(find "$BUILD" -name "*.a" -print0 2>/dev/null)
fi
if [ "$found" -eq 0 ]; then
  echo "No .o file referenced $SYM (or nm not found). Try:"
  echo "  1. Run a full build and re-run this script."
  echo "  2. Grep generated sources: grep -r 'dts_ord_$ORD\\|device_dts_ord_$ORD' $BUILD/zephyr --include='*.c' --include='*.h'"
  echo "  3. In devicetree_generated.h (in build) search for _ORD $ORD to see which node has that ordinal."
fi
