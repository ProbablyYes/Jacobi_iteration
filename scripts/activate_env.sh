#!/usr/bin/env bash
# shellcheck shell=bash

jacobi_repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export PATH="${jacobi_repo_dir}/.local/openmpi/bin:${jacobi_repo_dir}/.venv/bin:${PATH}"
export LD_LIBRARY_PATH="${jacobi_repo_dir}/.local/openmpi/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export MANPATH="${jacobi_repo_dir}/.local/openmpi/share/man${MANPATH:+:${MANPATH}}"
export PYTHONPATH="${jacobi_repo_dir}/.local/python${PYTHONPATH:+:${PYTHONPATH}}"
unset jacobi_repo_dir
