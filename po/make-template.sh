#!/bin/sh
# This script makes a translation template
# The reason for this not being inside CMakeLists.txt
# is that the translator should not need to run
# the whole configure process to get this single stupid file.

# Get the package name from CMakeLists.txt
PACKAGE=$(sed -n '/^[ \t]*[pP][rR][oO][jJ][eE][cC][tT][ \t]*([ \t]*\([^ \t)]\{1,\}\).*).*/{s//\1/p;q}' \
	../CMakeLists.txt)

# Get the package version from CMakeLists.txt
EXP_BEG='/^[ \t]*[sS][eE][tT][ \t]*([ \t]*project_VERSION_'
EXP_END='[ \t]\{1,\}"\{0,1\}\([^)"]\{1,\}\)"\{0,1\}).*/{s//\1/p;q}'

MAJOR=$(sed -n "${EXP_BEG}MAJOR${EXP_END}" ../CMakeLists.txt)
MINOR=$(sed -n "${EXP_BEG}MINOR${EXP_END}" ../CMakeLists.txt)
PATCH=$(sed -n "${EXP_BEG}PATCH${EXP_END}" ../CMakeLists.txt)

if [ "$MAJOR" != "" ]; then
	VERSION=$MAJOR
	if [ "$MINOR" != "" ]; then
		VERSION=$VERSION.$MINOR
		if [ "$PATCH" != "" ]; then
			VERSION=$VERSION.$PATCH
		fi
	fi
fi

# Finally make the template
xgettext -LC -k_ -kN_ ../src/*.c -o $PACKAGE.pot \
	--package-name="$PACKAGE" --package-version="$VERSION" \
	--copyright-holder="Přemysl Janouch"

