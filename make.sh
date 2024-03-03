#!/bin/bash

###########################################################
###########################################################
RM="rm -rfd"
RED='\033[0;31m'
NC='\033[0m'
GREEN='\033[0;32m'

###########################################################
###########################################################
function push_environs() {
    # Push environment variables to the server
    CMD="npx dotenv-vault@latest push"
    for stage in "development" "production" "ci" "staging"; do
        CMD_FINAL="${CMD} ${stage}"
        if [ $DEBUG -eq 1 ]; then echo "$CMD_FINAL"; else eval "$CMD_FINAL"; fi
    done
}

function delete_local_environs() {
    # Delete environment variables from the server
    CMD="rm"
    for stage in ".development" ".production" ".ci" ".staging" ""; do
        CMD_FINAL="${CMD} .env${stage}"
        if [ $DEBUG -eq 1 ]; then echo "$CMD_FINAL"; else eval "$CMD_FINAL"; fi
    done
}

function pull_environs() {
    # Pull environment variables from the server
    CMD="npx dotenv-vault@latest pull"
    for stage in "development" "production" "ci" "staging"; do
        CMD_FINAL="${CMD} ${stage}"
        if [ $DEBUG -eq 1 ]; then echo "$CMD_FINAL"; else eval "$CMD_FINAL"; fi
    done
}

###########################################################
###########################################################
function clean() {
    # Clean project folder in order to see what will be done, set env variable $DEBUG=1
    ${RM} *.zip
    # Folders
    for folder in \
            "venv" \
            "*__pycache__" \
            "*.ruff_cache" \
            "*.pytest_cache" \
            "*.cache" \
            "*htmlcov*" \
            "skip-covered"\
            ; do
        if [ "$DEBUG" -eq 1 ]; then find . -type d -iname "${folder}"; else find . -type d -iname "${folder}" | xargs ${RM} -rf; fi
    done
    # Files
    for file in \
            "*.DS_Store" \
            "tags" \
            "db.sqlite3" \
            "*.png" \
            "*.zip" \
            "*.log" \
            "coverage.xml" \
            "*.coverage" \
            "coverage.lcov" \
            ; do
        if [ "$DEBUG" -eq 1 ]; then find . -type f -iname "${file}"; else find . -type f -iname "${file}" | xargs ${RM}; fi
    done
}

function rsync_to_server() {
    # Rsync to the server
    # ENVIRONMENT VARIABLES
    #   - PROJECT_PATH
    #   - SERVER_URL
    #   - USER
    #   - DEBUG

    # Set environment variables
    if [ -z "$PROJECT_PATH" ]; then PROJECT_PATH="~/repos/prl-1"; fi
#    if [ -z "$SERVER_URL" ]; then SERVER_URL="merlin.fit.vutbr.cz"; fi # eva.fit.vutbr.cz
    if [ -z "$SERVER_URL" ]; then SERVER_URL="eva.fit.vutbr.cz"; fi
    if [ -z "$USER_SERVER" ]; then USER_SERVER="xlapes02"; fi

    CMD1="mkdir -p '${PROJECT_PATH}'"

    ssh "${USER_SERVER}@${SERVER_URL}" "${CMD1}"

    rsync --archive --verbose --compress --delete \
        pms.cpp \
        CMakelists.txt \
        docker-compose.yml Dockerfile \
        make.sh README.md generateNums.sh \
        "${USER_SERVER}@${SERVER_URL}:${PROJECT_PATH}"
}

function help() {
    # Print usage on stdout
    echo "Available functions:"
    for file in "make.sh"; do
        function_names=$(cat ${file} | grep -E "(\ *)function\ +.*\(\)\ *\{" | sed -E "s/\ *function\ +//" | sed -E "s/\ *\(\)\ *\{\ *//")
        for func_name in ${function_names[@]}; do
            printf "    $func_name\n"
            awk "/function ${func_name}()/ { flag = 1 }; flag && /^\ +#/ { print \"        \" \$0 }; flag && !/^\ +#/ && !/function ${func_name}()/  { print "\n"; exit }" ${file}
        done
    done
}

function usage() {
    # Print usage on stdout
    help
}

function die() {
    # Print error message on stdout and exit
    printf "${RED}ERROR: $1${NC}\n"
    help
    exit 1
}

function main() {
    # Main function: Call other functions based on input arguments
    [[ "$#" -eq 0 ]] && die "No arguments provided"
    while [ "$#" -gt 0 ]; do
        "$1" || die "Unknown function: $1()"
        shift
    done
}

function prune_volumes() {
    # Stop and remove all containers
    docker stop $(docker ps -aq)
    docker rm $(docker ps -aq)

    # Remove all volumes: not just dangling ones
    for i in $(docker volume ls --format json | jq -r ".Name"); do
        docker volume rm -f ${i}
    done
}

function prune_not_images() {
    # Stop and remove all containers
    docker stop $(docker ps -aq)
    docker system prune --force --volumes

    # Remove all volumes: not just dangling ones
    for i in $(docker volume ls --format json | jq -r ".Name"); do
        docker volume rm -f ${i}
    done
}

function prune() {
    # Stop and remove all containers
    docker stop $(docker ps -aq)
    docker system prune --all --force --volumes

    # Remove all volumes: not just dangling ones
    for i in $(docker volume ls --format json | jq -r ".Name"); do
        docker volume rm -f ${i}
    done
}
main "$@"
