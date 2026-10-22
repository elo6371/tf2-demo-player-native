#!/usr/bin/env bash
# What does a NATIVE crash look like as a shell exit code on this host?
# The question run 2 raised: scroll.rc=127 cannot be "command not found" because
# the trace proves the process ran, so 127 has to be something else.
nonexistent_command_abc 2>/dev/null; echo "notfound_rc=$?"
bash -c 'kill -SEGV $$'; echo "segv_rc=$?"
bash -c 'kill -ABRT $$'; echo "abrt_rc=$?"
