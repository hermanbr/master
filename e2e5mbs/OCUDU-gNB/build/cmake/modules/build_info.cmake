# SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI

execute_process(
COMMAND git rev-parse --abbrev-ref HEAD
WORKING_DIRECTORY "/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu"
OUTPUT_VARIABLE GIT_BRANCH
OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
COMMAND git log -1 --format=%h
WORKING_DIRECTORY "/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu"
OUTPUT_VARIABLE GIT_COMMIT_HASH
OUTPUT_STRIP_TRAILING_WHITESPACE
)

message(STATUS "Generating build information")
configure_file(
  /home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/lib/support/versioning/hashes.h.in
  /home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/hashes.h
)
