#!/usr/bin/env bash
#
# test_symlinks.sh
#
# A script to test the various options of the `symlinks` utility,
# extended to include "weirder" broken/circular symlinks.
#
# Usage:
#   chmod +x test_symlinks.sh
#   ./test_symlinks.sh
#

################################################################################
# Configuration
################################################################################

# Point this to your compiled symlinks binary if not in PATH
SYMLINKS_BINARY="build/symlinks"

# Name of the test directory that will be created
TESTDIR="symlinks_test"

# Global flag to track if any test fails
FAIL=0

################################################################################
# Setup / Cleanup Functions
################################################################################

create_test_env() {
  echo "Creating test environment: $TESTDIR"
  rm -rf "$TESTDIR"
  mkdir -p "$TESTDIR/subdir"

  # Create some regular files
  touch "$TESTDIR/file1"
  touch "$TESTDIR/subdir/file2"

  # 1) A relative link pointing to file1
  ln -s file1 "$TESTDIR/rel_link"

  # 2) An absolute link (e.g., to /tmp)
  ln -s /tmp "$TESTDIR/abs_link"

  # 3) A dangling link (target does not exist)
  ln -s /nonexistent "$TESTDIR/dangling"

  # 4) A "messy" link with unnecessary references (./subdir/../.)
  ln -s ./subdir/../subdir/ "$TESTDIR/messy_link"

  # 5) A "lengthy" link with extra ../ (fake path to emulate a complex chain)
  ln -s ./subdir "$TESTDIR/lengthy_link"

  # 6) A symlink pointing to another symlink
  #    We'll have symlink -> "rel_link" for added fun.
  ln -s rel_link "$TESTDIR/symlink_to_symlink"

  # 7) A self-referential symlink (a symlink that points to itself)
  #    This is instantly broken/circular from the OS perspective,
  #    but interesting for testing.
  ln -s self_link "$TESTDIR/self_link"
  (cd "$TESTDIR" && ln -sf self_link self_link)

  # 8) A circular loop of two symlinks referencing each other
  ln -s loop_b "$TESTDIR/loop_a"
  ln -s loop_a "$TESTDIR/loop_b"

  # 9) A deep relative path that doesn't exist (dangling, multi-level)
  mkdir -p "$TESTDIR/subdir2/subsubdir"
  ln -s ../../nonexistent_dir/deeper/file.txt "$TESTDIR/subdir2/subsubdir/deep_dangling"

  # 10) Another absolute path link that doesn't exist (dangling)
  ln -s /an/absolute/path/that/does/not/exist "$TESTDIR/abs_dangling"

  # 11) A symlink pointing inside subdir that is actually a symlink
  #     We’ll create subdir_link -> subdir, then link_into_subdir -> subdir_link/file2
  ln -s subdir "$TESTDIR/subdir_link"
  ln -s subdir_link/file2 "$TESTDIR/link_into_subdir"
}

check_binary() {
  if [ ! -x "$SYMLINKS_BINARY" ]; then
    echo "ERROR: symlinks binary not found or not executable at: $SYMLINKS_BINARY"
    FAIL=1
  fi
}

################################################################################
# Test Cases (mostly using -x)
################################################################################

test_basic_scan() {
  echo "==== Test 1: Basic Scan (no options, but debug mode) ===="
  create_test_env
  "$SYMLINKS_BINARY" -x "$TESTDIR"
  if [ $? -ne 0 ]; then
    echo "Test 1 failed."
    FAIL=1
  fi
  echo
}

test_verbose() {
  echo "==== Test 2: Verbose (-v) ===="
  create_test_env
  "$SYMLINKS_BINARY" -x -v "$TESTDIR"
  if [ $? -ne 0 ]; then
    echo "Test 2 failed."
    FAIL=1
  fi
  echo
}

test_recurse() {
  echo "==== Test 3: Recurse (-r) ===="
  create_test_env
  "$SYMLINKS_BINARY" -x -r "$TESTDIR"
  if [ $? -ne 0 ]; then
    echo "Test 3 failed."
    FAIL=1
  fi
  echo
}

test_convert() {
  echo "==== Test 4: Convert (-c) ===="
  create_test_env
  "$SYMLINKS_BINARY" -x -c "$TESTDIR"
  if [ $? -ne 0 ]; then
    echo "Test 4 failed."
    FAIL=1
  fi
  echo
}

test_delete_dangling() {
  echo "==== Test 5: Delete Dangling (-d) ===="
  create_test_env
  "$SYMLINKS_BINARY" -x -d "$TESTDIR"
  if [ $? -ne 0 ]; then
    echo "Test 5 failed."
    FAIL=1
  fi
  echo
}

test_shorten() {
  echo "==== Test 6: Shorten (-s) (detect only) ===="
  create_test_env
  "$SYMLINKS_BINARY" -x -s "$TESTDIR"
  if [ $? -ne 0 ]; then
    echo "Test 6 failed."
    FAIL=1
  fi
  echo
}

test_shorten_convert() {
  echo "==== Test 7: Convert + Shorten (-c -s) ===="
  create_test_env
  "$SYMLINKS_BINARY" -x -cs "$TESTDIR"
  if [ $? -ne 0 ]; then
    echo "Test 7 failed."
    FAIL=1
  fi
  echo
}

test_other_fs() {
  echo "==== Test 8: Other Filesystem (-o) ===="
  echo "Note: This requires a link to a different filesystem to fully test."
  create_test_env
  "$SYMLINKS_BINARY" -x -o "$TESTDIR"
  if [ $? -ne 0 ]; then
    echo "Test 8 failed."
    FAIL=1
  fi
  echo
}

test_test_mode() {
  echo "==== Test 9: Test Mode (-t) ===="
  create_test_env
  # Combine some interesting options so we can see what *would* happen
  "$SYMLINKS_BINARY" -x -rct "$TESTDIR"
  if [ $? -ne 0 ]; then
    echo "Test 9 failed."
    FAIL=1
  fi
  echo
}

################################################################################
# Verification Helper (allowing for path equivalences)
################################################################################

# verify_symlink_equiv <link> <expected_target>
#
# Uses readlink to get the symlink's raw string,
# and realpath (with -m) to see if both the actual
# link target and <expected_target> resolve to the same absolute path.
verify_symlink_equiv() {
  local linkpath="$1"
  local expected_raw="$2"

  # Make sure the link actually exists and is a symlink
  if [ ! -L "$linkpath" ]; then
    echo "FAIL: $linkpath is not a symlink (expected)."
    FAIL=1
    return
  fi

  # The string that the link actually points to
  local actual_string
  actual_string="$(readlink "$linkpath")"

  # We'll convert both the actual link target and the expected target
  # into absolute paths *relative to the symlink's directory*.
  local link_dir
  link_dir="$(dirname "$linkpath")"

  local actual_resolved
  local expected_resolved

  actual_resolved="$(realpath -m "$link_dir/$actual_string")"
  expected_resolved="$(realpath -m "$link_dir/$expected_raw")"

  if [ "$actual_resolved" != "$expected_resolved" ]; then
    echo "FAIL: Symlink $linkpath resolves to '$actual_resolved' but we expected '$expected_resolved'"
    echo "      (raw link text was '$actual_string')"
    FAIL=1
  else
    echo "OK: Symlink $linkpath resolves to '$actual_resolved' as expected."
  fi
}

################################################################################
# Test symlinks *without* the -x option, verifying via realpath
################################################################################
test_no_debug_mode() {
  echo "==== Test 10: Normal Operation (no -x), then verify ===="
  create_test_env

  # We'll run symlinks with -r -c -s so we can see changes happen
  "$SYMLINKS_BINARY" -r -c -s "$TESTDIR"
  if [ $? -ne 0 ]; then
    echo "Test 10 failed (command returned non-zero)."
    FAIL=1
  fi

  echo "Verifying that symlinks were updated properly:"

  # We'll verify a couple of known links that should end up normalized to 'subdir'
  verify_symlink_equiv "$TESTDIR/messy_link" "subdir"
  verify_symlink_equiv "$TESTDIR/lengthy_link" "subdir"

  echo
}

################################################################################
# Main
################################################################################
rm -rf build
meson setup build --reconfigure
meson compile -C build

check_binary

test_basic_scan
test_verbose
test_recurse
test_convert
test_delete_dangling
test_shorten
test_shorten_convert
test_other_fs
test_test_mode
test_no_debug_mode

echo "All tests completed."

# Return 1 if any test failed; otherwise 0
exit $FAIL
