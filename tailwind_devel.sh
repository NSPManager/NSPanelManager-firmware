#!/bin/bash
set -eou pipefail

npx tailwindcss -i ./data/static/build.css -o ./data/static/main.css --watch
