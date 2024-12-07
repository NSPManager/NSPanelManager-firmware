#!/bin/bash
set -eou pipefail

pipenv run platformio run -t compiledb
