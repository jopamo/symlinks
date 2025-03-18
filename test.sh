#!/usr/bin/env bash
#
# test_symlinks.sh
#
# A simple script to test the various options of the `symlinks` utility.
# It creates a small test directory tree with different kinds of symlinks
# (relative, absolute, dangling, messy, and lengthy) and then runs symlinks
# under different option combinations.
#
# Usage:
#   chmod +x test_symlinks.sh
#   ./test_symlinks.sh
#

################################################################################
# Configuration
################################################################################

# Point this to your compiled symlinks binary if not in PATH
SYMLINKS_BINARY="./symlinks"

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

  # Create a variety of symbolic links

  # 1) A relative link pointing to file1
  ln -s file1 "$TESTDIR/rel_link"

  # 2) An absolute link (e.g., to /tmp)
  ln -s /tmp "$TESTDIR/abs_link"

  # 3) A dangling link (target does not exist)
  ln -s /nonexistent "$TESTDIR/dangling"

  # 4) A "messy" link with unnecessary references (./subdir/../.)
  ln -s ./subdir/../. "$TESTDIR/messy_link"

  # 5) A "lengthy" link with extra ../
  ln -s ../subdir/ "$TESTDIR/lengthy_link"
}

check_binary() {
  if [ ! -x "$SYMLINKS_BINARY" ]; then
    echo "ERROR: symlinks binary not found or not executable at: $SYMLINKS_BINARY"
    FAIL=1
  fi
}

################################################################################
# Test Cases
################################################################################

test_basic_scan() {
  echo "==== Test 1: Basic Scan (no options) ===="
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
  echo "We will just run symlinks with -o, but the test environment may not"
  echo "demonstrate 'other_fs' unless you create cross-device symlinks."
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
# Main
################################################################################

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

echo "All tests completed."

# Return 1 if any test failed; otherwise 0
exit $FAIL
