#!/bin/sh

EXEC="./dm"

for F in "$@"
do

	if [[ ! "$F" =~ .*\.WAV ]]; then
		echo "Skipped: $F"
		continue
	fi

	TIMESTAMP=$(GetFileInfo -m "$F")

	F2="${F%.WAV}_MONO.WAV"

	echo "Processing: $F"

	$EXEC "$F" "$F2"

	SetFile -d "$TIMESTAMP" -m "$TIMESTAMP" "$F2"

done
