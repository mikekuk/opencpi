#!/bin/bash --noprofile
# This file is protected by Copyright. Please refer to the COPYRIGHT file
# distributed with this source distribution.
#
# This file is part of OpenCPI <http://www.opencpi.org>
#
# OpenCPI is free software: you can redistribute it and/or modify it under the
# terms of the GNU Lesser General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option) any
# later version.
#
# OpenCPI is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License along
# with this program. If not, see <http://www.gnu.org/licenses/>.

##########################################################################################
# This script produces a contents list for one of the distribution packages
# It contains the rules for the contents in each one.
# The output is processed by various packaging systems, including rpm and tar
#
# The choices are:
# all:     everything in exports and everything in projects in the git repo
# runtime: minimal runtime (plus some tests) for the platform(s)
# devel:   development configuration minus runtime
#
# Basically we generate a set of directories and files for export that are
# minimal for the export type.

# The inputs are the type of export (arg1), platforms (arg2), and cross (arg3)
# The third arg is just an empty/non-type boolean indicator

# The result is a set of directories and files, one per line.
# each line is of the form
# <source-path> [<dest-path>]

# Much like the "ls -F" command:
# If there is a @ at the end, it indicates a symlink that should REMAIN linked in destination
# If there is a / at the end, it indicates a directory that should be owned by the package
# I.e. the "devel" package might have files in directories that are owned by the runtime package.

[ "$1" = -v ] && verbose=1 && shift
type=$1
platforms=$2
cross=$3
[ -z "${platforms}" ] && echo "Don't run this by hand." && exit 1
set -e
shopt -s dotglob
shopt -s nullglob

function found_in {
  local look=$1
  shift
  for i in $*; do [ $i = $look ] && return 0; done
  return 1
}

function is_platform {
  [ -d $1/lib -o -d $1/bin ]
}

function skip_platform {
  local base=$(basename $1)
  if [ -n "$cross" ]; then
    ! is_platform $1 || ! found_in $base $platforms
  else
    is_platform $1 && ! found_in $base $platforms
  fi
}

function any_repo {
  git ls-files --error-unmatch $1 > /dev/null 2>&1
}
# emit the dir name if the tree is clean, otherwise emit each cached item here and recurse on
# directories.  Basically this is: git ls-files but don't descend when all contents are cached
function emit_project_dir {
  set -o pipefail
  any_repo $1 || return 1
  if diff -q <(find $1 -type f | sort) <(git ls-files $1 | sort) >/dev/null; then
    [ -n "$verbose" ] && echo Dir at $1 is the same >&2
    echo $1@
  else
    [ -n "$verbose" ] && echo Dir at $1 is different, descending >&2
    found=
    for i in $1/*; do
      any_repo $i || continue
      if [ -d $i -a ! -L $i ]; then
        emit_project_dir $i && found=1
      else
        echo $i@
        found=1  
      fi
    done
    [ -n "$found" ] && echo $1/
  fi
}

[ $1 = test ] && {
  emit_project_dir $2
  exit $?
}

for l in `find cdk -follow -type l`; do
  bad=1
  echo Dead exports link found: $l
done
for l in `find -H . -name "-*"`; do
  bad=1
  echo Found files starting with hyphen
done
[ -n "$bad" ] && exit 1

# FIXME: this list is redundant with "install-prerequisites.sh" and "places"
# This list could potentially be platform-specific
# and then there are platform-specific prereqs
prereqs="gmp lzma gtest patchelf inode64 ad9361 liquid"
case $type in
  all)
    for f in cdk/*; do
      [ $f != cdk/runtime ] && ! skip_platform $f && echo $f
    done
    for p in $prereqs; do
      for d in prerequisites/$p/*; do
        skip_platform $d || echo $d
      done
    done;;
  runtime)
    for f in cdk/runtime/*; do
      skip_platform $f && continue
      echo $f cdk
      [ -d $f ] && (cd cdk/runtime;
                    find $(basename $f) -type d -exec echo cdk/runtime/{}/ cdk/{} \; )
    done
    # runtime prereqs are only shared libraries
    found1=
    for p in $prereqs; do
      found=
      # We explicitly only look for shared libraries in a platform's lib directory
      # Prerequisite libraries in runtime are currently *solely* needed when RCC
      # workers have asked to use them.  When we are delivering a dynamic runtime
      # they will be in OUR lib directory.
      for d in prerequisites/$p/*; do
        (skip_platform $d || ! is_platform $d || [ ! -d $d/lib ] ) && continue
	libs=$(echo $d/lib/*.{so,so.*,dylib})
        [ -n "$libs" ] && {
	  echo $libs | xargs -n 1 echo
	  echo $d/lib/
	  echo $d/
          found=1
          found1=1
        }
      done
      [ -n "$found" ] && echo prerequisites/$p/
    done
    [ -n "$found1" ] && echo prerequisites/
    ;;
  devel)
    for f in cdk/*; do
      ( [ $f = cdk/runtime ] || skip_platform $f ) && continue
      # If its not platform specific, it won't be in the cross-platform devel
      [ -n "$cross" -a $f != cdk/$platforms ] && continue
      (cd cdk; find -H $(basename $f)) | while read path; do
	 [ -e cdk/runtime/$path ] || echo cdk/$path$([ -d cdk/$path ] && echo /)
      done
    done
    for p in $prereqs; do
      for d in prerequisites/$p/*; do
        skip_platform $d && continue;
        if is_platform $d; then
          [ -z "$cross" -a -d $d/bin ] && find $d/bin ! -type d &&
	    find $d/bin -type d -exec echo {}/ \;
          [ -d $d/include ] && find $d/include ! -type d &&
            find $d/include -type d -exec echo {}/ \;
          [ -d $d/lib ] && find $d/lib -name "*.a" && 
	    [ -z "$(echo $d/lib/*.{so,so.*,dylib})" ] &&
            find $d/lib -type d -exec echo {}/ \;
        elif [ -z "$cross" ] && [[ $d == */include ]]; then
	  echo $d
          echo $d/
        fi
      done
    done
    if [ -z "${cross}" ]; then
      # emit project stuff that are git repo items
      git ls-files project-registry | sed 's/$/@/'
      echo project-registry/
      echo projects/
      emit_project_dir projects/core
      emit_project_dir projects/assets
    fi
    ;;
  driver)
    # this is NOT copying exports for now.
    (
     git ls-files os/linux/driver
     # this could be read by looking at depenedencies some day
     for f in runtime/hdl/include/Hdl{NetDefs,OCCP,PciDriver}.h \
             os/interfaces/include/KernelDriver.h COPYRIGHT LICENSE.txt; do
       echo $f
      done
    ) | sed 's/$/ driver/'
    echo driver/ driver
    ;;
  *) echo "Unknown export type";;
esac
