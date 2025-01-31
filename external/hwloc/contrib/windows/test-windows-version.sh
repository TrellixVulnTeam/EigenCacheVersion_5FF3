#!/bin/bash
#-*-sh-*-

#
# Copyright © 2018 Inria.  All rights reserved.
# See COPYING in top-level directory.
#
HWLOC_top_builddir="/home/bgoglin/SOFT/hwloc/tmp/hwloc2"
HWLOC_top_srcdir="/home/bgoglin/SOFT/hwloc/tmp/hwloc2"
windows_config_h="$HWLOC_top_srcdir/contrib/windows/hwloc_config.h"
config_h="$HWLOC_top_builddir/include/hwloc/autogen/config.h"
vcxproj_file="$HWLOC_top_srcdir/contrib/windows/libhwloc.vcxproj"
version_file="$HWLOC_top_srcdir/VERSION"

# get all individual version components
echo "Looking for Windows-specific version in $windows_config_h ..."
windows_major=$(grep -w HWLOC_VERSION_MAJOR $windows_config_h | grep -oP '[0-9]+')
windows_minor=$(grep -w HWLOC_VERSION_MINOR $windows_config_h | grep -oP '[0-9]+')
windows_release=$(grep -w HWLOC_VERSION_RELEASE $windows_config_h | grep -oP '[0-9]+')
windows_greek=$(grep -w HWLOC_VERSION_GREEK $windows_config_h | grep -oP '".*"' | tr -d \")
if [ -z "$windows_major" -o -z "$windows_minor" -o -z "$windows_release" ]; then
	# greek is likely empty on purpose, ignore it
	echo "ERROR: Failed to get Windows-specific HWLOC_VERSION_MAJOR/MINOR/RELEASE"
	exit 1
fi
echo "  Found major=$windows_major minor=$windows_minor release=$windows_release greek=$windows_greek"

# check that the version string matches
windows_version=$(grep -w HWLOC_VERSION $windows_config_h | grep -oP '".+"' | tr -d \")
echo "  Found windows-specific HWLOC_VERSION \"$windows_version\""
expected_windows_version="$windows_major.$windows_minor.$windows_release$windows_greek"
if [ "$windows_version" != "$expected_windows_version" ]; then
	echo "ERROR: Windows-specific HWLOC_VERSION \"$windows_version\" doesn't match HWLOC_VERSION_MAJOR/MINOR/RELEASE/GREEK components \"$expected_windows_version\""
	exit 1
fi
echo "  Windows-specific HWLOC_VERSION \"$windows_version\" matches HWLOC_VERSION_MAJOR/MINOR/RELEASE/GREEK components"

# check that it matchs the official version, without a GREEK
echo "Looking in $config_h ..."
official_major=$(grep -w HWLOC_VERSION_MAJOR $config_h  | grep -oP '[0-9]+')
official_minor=$(grep -w HWLOC_VERSION_MINOR $config_h  | grep -oP '[0-9]+')
official_release=$(grep -w HWLOC_VERSION_RELEASE $config_h  | grep -oP '[0-9]+')
if [ -z "$official_major" -o -z "$official_minor" -o -z "$official_release" ]; then
	echo "ERROR: Failed to get official HWLOC_VERSION_MAJOR/MINOR/RELEASE"
	exit 1
fi
echo "  Found major=$official_major minor=$official_minor release=$official_release"
official_version_nogreek="$official_major.$official_minor.$official_release"
if [ "$official_version_nogreek" != "$windows_version" ]; then
	echo "ERROR: Windows-specific HWLOC_VERSION \"$windows_version\" doesn't match \"$official_version_nogreek\" without GREEK"
	exit 1
fi
echo "Windows-specific HWLOC_VERSION \"$windows_version\" matches official version without greek"

echo

# get the windows soname
echo "Looking in $vcxproj_file ..."
if [ `grep '<TargetName>' $vcxproj_file | uniq -c | wc -l` != 1 ]; then
	echo "ERROR: Couldn't find a single value for <TargetName> lines"
	exit 1
fi
windows_lib_soname=$(grep -m1 '<TargetName>' $vcxproj_file | grep -oP '\d+')
if [ -z "$windows_lib_soname" ]; then
	echo "ERROR: Failed to get the Windows-specific soname"
	exit 1
fi
echo "  Found Windows-specific soname $windows_lib_soname"

# get the official soname
echo "Looking in $version_file ..."
official_lib_version=$(grep -w "libhwloc_so_version" $version_file | grep -oP '\d+:\d+:\d+')
if [ -z "$official_lib_version" ]; then
	echo "ERROR: Failed to get the official lib version"
	exit 1
fi
echo "  Found official lib version \"$official_lib_version\""

# bashisms to extract the soname from the version
IFS=':' arr=(${official_lib_version})
declare -i official_lib_soname=${arr[0]}-${arr[2]}
echo "  Extracted official lib soname $official_lib_soname"

# check that sonames match only if on a release branch
if [ "$official_lib_version" != "0:0:0" ] ; then
	if [ "$windows_lib_soname" != "$official_lib_soname" ]; then
		echo "ERROR: Windows-specific lib soname $windows_lib_soname differs from $official_lib_soname (from \"$official_lib_version\")"
		exit 1
	fi
	echo "Windows-specific lib soname $windows_lib_soname matches official lib soname"
else
	echo "Ignoring unset lib soname"
fi
