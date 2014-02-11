#!/bin/sh

# Copyright (c) 2014 Yubico AB
# All rights reserved.
# 
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
# Additional permission under GNU GPL version 3 section 7
#
# If you modify this program, or any covered work, by linking or
# combining it with the OpenSSL project's OpenSSL library (or a
# modified version of that library), containing parts covered by the
# terms of the OpenSSL or SSLeay licenses, We grant you additional 
# permission to convey the resulting work. Corresponding Source for a
# non-source form of such a combination shall include the source code
# for the parts of OpenSSL used as well as that of the covered work.

# This is a _very_ simple test shell script, really only verifying
#  that we managed to build a binary and it can execute.

set -e

HELP_OUTPUT=$(../yubico-piv-tool$EXEEXT --help)

expected="yubico-piv-tool $VERSION"
VERSION_OUTPUT=$(../yubico-piv-tool$EXEEXT --version | sed 's/\r//')
if [ "x$VERSION_OUTPUT" != "x$expected" ]; then
  echo "Version ($VERSION_OUTPUT) not matching expected output $expected."
  exit 1
fi
