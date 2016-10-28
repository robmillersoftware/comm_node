#!/bin/bash
SOURCE="${BASH_SOURCE[0]}"
export INSTALL_DIRECTORY="$( cd -P "$( dirname "$SOURCE" )" && pwd )"
export DEFAULT_INTERFACE="enp0s3"

$INSTALL_DIRECTORY/bin/commNode &
