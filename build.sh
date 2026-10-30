#!/bin/sh
# Construit les exécutables Mac (Apple Silicon + Intel) et Windows dans dist/
set -e
cd "$(dirname "$0")"
mkdir -p dist
GOOS=darwin  GOARCH=arm64 go build -ldflags="-s -w" -o dist/StageWatch-mac-arm64 .
GOOS=darwin  GOARCH=amd64 go build -ldflags="-s -w" -o dist/StageWatch-mac-intel .
GOOS=windows GOARCH=amd64 go build -ldflags="-s -w" -o dist/StageWatch.exe .
ls -lh dist
