#!/bin/bash

# Chronon3d Development Watcher
# Rebuilds and renders when source changes are detected.

COMP_ID=$1
if [ -z "$COMP_ID" ]; then
    echo "Usage: $0 <composition_id>"
    exit 1
fi

BIN="./bin/chronon3d"
if [ ! -f "$BIN" ]; then
    # Try xmake path if not in ./bin
    BIN="xmake run chronon3d"
fi

echo "Watching for changes in src/ and include/ ..."
echo "Composition: $COMP_ID"

# Initial run
xmake -y && $BIN render "$COMP_ID" --frames 0 -o "output/watch_####.png" --diagnostic

# Watch using inotifywait if available, otherwise fallback to polling
if command -v inotifywait >/dev/null 2>&1; then
    while inotifywait -r -e modify src include apps; do
        echo "Change detected! Rebuilding..."
        if xmake -y; then
            $BIN render "$COMP_ID" --frames 0 -o "output/watch_####.png" --diagnostic
        else
            echo "Build failed. Fix errors to continue."
        fi
    done
else
    echo "inotify-tools not found. Falling back to simple loop (less efficient)."
    LAST_SUM=""
    while true; do
        CUR_SUM=$(find src include apps -type f -exec md5sum {} + | md5sum)
        if [ "$CUR_SUM" != "$LAST_SUM" ]; then
            if [ -n "$LAST_SUM" ]; then
                echo "Change detected! Rebuilding..."
                if xmake -y; then
                    $BIN render "$COMP_ID" --frames 0 -o "output/watch_####.png" --diagnostic
                else
                    echo "Build failed."
                fi
            fi
            LAST_SUM=$CUR_SUM
        fi
        sleep 1
    done
fi
