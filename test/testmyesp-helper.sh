#!/bin/bash

set -e

server_url=""
username=""
password=""
timeout="300"
wait="true"
continue_on_failure="false"
show_serial_log="false"
verbose="false"
no_colors="false"
flash_images=()
assets=()
test_cases=()
vars_file=""

exit_code=0

red='\033[0;31m'
green='\033[0;32m'
white='\033[1;37m'
endc='\033[0m'  # color end


function exit_usage() {
    echo "Usage: $0 [options...]"
    echo ""
    echo "Available options:"
    echo "    --server-url <url>"
    echo "    --credentials <username>:<password>"
    echo "    --credentials-file <file.txt>"
    echo "    --timeout <timeout> (defaults to 300s)"
    echo "    --no-wait"
    echo "    --continue-on-failure"
    echo "    --verbose"
    echo "    --show-serial-log"
    echo "    --no-colors"
    echo "    --flash-image-url <name> <address> <url>"
    echo "    --flash-image-file <name> <address> <file.bin>"
    echo "    --flash-image-fill <name> <address> <fill> <size>"
    echo "    --asset <name> <file.ext>"
    echo "    --test-case <file.json>"
    echo "    --test-cases <file1.json> <file2.json> ..."
    echo "    --vars-file <file>"
    echo ""
    echo "The following options are required:"
    echo "    --server-url"
    echo "    --credentials or --credentials-file"
    echo "    --test-case or --test-cases"
    echo ""
    echo "The following options can be repeated:"
    echo "    --flash-image-url"
    echo "    --flash-image-file"
    echo "    --flash-image-fill"
    echo "    --asset"
    echo "    --test-case"
    
    exit 1
}

function check_required_commands() {
    for cmd in openssl jq; do
        if ! which ${cmd} &>/dev/null; then
            echo "Please make sure \"${cmd}\" is installed on this machine."
            exit 1
        fi
    done
}

function base64_url() {
	# Use tr to URL encode the output from base64.
	base64 | tr -d '=' | tr '/+' '_-' | tr -d '\n'
}

function hmacsha256() {
    # $1 - password
	openssl dgst -binary -sha256 -hmac "$1"
}

function make_jwt() {
    # $1 - username
    # $2 - password
    
    header='{"typ": "JWT","alg": "HS256"}'
    payload="{\"sub\": \"$1\"}"
    
    header_base64=$(echo -n "${header}" | base64_url)
    payload_base64=$(echo -n "${payload}" | base64_url)
    signature_base64=$(echo -n "${header_base64}.${payload_base64}" | hmacsha256 "$2" | base64_url)
    
    echo "${header_base64}.${payload_base64}.${signature_base64}"
}

function make_request_data_file() {
    data_file=$(mktemp)
    
    echo "{"                                                    >> ${data_file}
    echo "\"timeout\": ${timeout},"                             >> ${data_file}
    echo "\"continue_on_failure\": ${continue_on_failure},"     >> ${data_file}
    echo "\"flash_images\": ["                                  >> ${data_file}
    
    # flash images
    count=1
    len=${#flash_images[@]}
    for flash_image in ${flash_images[@]}; do
        echo -n "${flash_image}"                                >> ${data_file}
        if [[ ${count} -lt ${len} ]]; then
            echo ","                                            >> ${data_file}
        else
            echo                                                >> ${data_file}
        fi
        count=$((count + 1))
    done

    echo "],"                                                   >> ${data_file}
    echo "\"assets\": ["                                        >> ${data_file}

    # assets
    count=1
    len=${#assets[@]}
    for asset in ${assets[@]}; do
        echo -n "${asset}"                                      >> ${data_file}
        if [[ ${count} -lt ${len} ]]; then
            echo ","                                            >> ${data_file}
        else
            echo                                                >> ${data_file}
        fi
        count=$((count + 1))
    done

    echo "],"                                                   >> ${data_file}
    echo "\"test_cases\": ["                                    >> ${data_file}

    # test cases
    count=1
    len=${#test_cases[@]}
    for test_case in ${test_cases[@]}; do
        cat "${test_case}" | envsubst                           >> ${data_file}
        if [[ ${count} -lt ${len} ]]; then
            echo ","                                            >> ${data_file}
        else
            echo                                                >> ${data_file}
        fi
        count=$((count + 1)) 
    done
    
    echo "]"                                                    >> ${data_file}
    echo "}"                                                    >> ${data_file}

    echo ${data_file}
}

function run_job() {
    echo "Running job..."

    if [[ -n "${vars_file}" ]]; then
        set -a
        source "${vars_file}"
    fi

    data_file=$(make_request_data_file)
    response_file=$(mktemp)
    jwt=$(make_jwt "${username}" "${password}")
    max_time=$((timeout + timeout / 10))
    query="wait=${wait}"
    
    status=$(curl -Ss -m ${max_time} --out ${response_file} -w "%{http_code}" \
                  -H "Authorization: Bearer ${jwt}" \
                  -H "Content-Type: application/json" \
                  "${server_url}/jobs?${query}" --data-binary "@${data_file}")

    if [[ "${status}" == 2* ]]; then
        show_results "$(cat ${response_file})"
    else
        if ! jq < ${response_file} 2>/dev/null; then
            cat ${response_file}
        fi
        exit_code=1
    fi

    rm ${response_file}
    rm ${data_file}
}

function show_results() {
    # $1 - results json

    declare -A job_summary

    output=$(jq -r 'with_entries(select([.key] |
                    inside(["id", "state", "added_moment", "started_moment", "ended_moment", "error"]))) |
                    to_entries | .[] | "job_" + .key + "=" + (.value | tostring)' <<< $1)

    while IFS== read -r key value; do
        job_summary[${key}]="${value}"
    done <<< ${output}

    start_timestamp=$(date --date="${job_summary["job_started_moment"]}" +%s)
    end_timestamp=$(date --date="${job_summary["job_ended_moment"]}" +%s)
    duration=$((${end_timestamp} - ${start_timestamp} + 1))

    if [[ "${job_summary["job_state"]}" != "succeeded" ]]; then
        exit_code=1
    fi

    echo
    echo -e "${white}---- job summary ----${endc}"
    echo "ID: ${job_summary["job_id"]}"
    echo "State: ${job_summary["job_state"]}"
    echo "Error: ${job_summary["job_error"]}"
    echo "Added: ${job_summary["job_added_moment"]}"
    echo "Started: ${job_summary["job_started_moment"]}"
    echo "Ended: ${job_summary["job_ended_moment"]}"
    echo "Duration: ${duration} s"
    echo

    tlen=${#test_cases[@]}
    for ((i = 0; i < tlen; i++)); do
        result=$(jq ".results[${i}]" <<< $1)

        declare -A result_summary

        output=$(jq -r 'with_entries(select([.key] |
                        inside(["name", "passed"]))) |
                        to_entries | .[] | "result_" + .key + "=" + (.value | tostring)' <<< ${result})

        while IFS== read -r key value; do
            result_summary[${key}]="${value}"
        done <<< ${output}

        passed=$(jq .passed <<< ${result})
        status="passed"
        color="green"
        passed=$(jq .passed <<< ${result})
        if [[ ${passed} != "true" ]]; then
            status="failed"
            color="red"
        fi

        echo -e "${result_summary["result_name"]} ($((i + 1))/${tlen}): ${!color}${status}${endc}"

        if [[ ${verbose} == "true" || ${passed} == "false" ]]; then
            echo -e "${white}---- test case \"${result_summary["result_name"]}\" result ----${endc}"
            echo "Instruction Log:"
            jq -r '.instruction_log | .[] | "    \(.moment): \(.level): [\(.instruction)] \(.msg)"' <<< ${result}

            if [[ "${show_serial_log}" == "true" ]]; then
                slog=$(jq -r .serial_log <<< ${result} | base64 -d)
                echo "Serial Log:"
                while read line; do echo "    ${line}"; done <<< ${slog}
            fi
        fi

        # stop at first failed test, if continue_on_failure is false
        if [[ ${continue_on_failure} == "false" && ${passed} == "false" ]]; then
            break;
        fi
    done

    echo
    echo -e "${white}---- job end ----${endc}"
    echo
}

function parse_options() {
    while [[ $# -gt 0 ]]; do
        opt="$1"

        case ${opt} in
            --server-url)
                test -z "$2" && exit_usage
                server_url="$2"
                shift 2
                ;;

            --credentials)
                test -z "$2" && exit_usage
                IFS=":" credentials=($2)
                unset IFS
                username="${credentials[0]}"
                password="${credentials[1]}"
                shift 2
                ;;

            --credentials-file)
                test -z "$2" && exit_usage
                credentials=$(<$2)
                IFS=":" credentials=(${credentials})
                unset IFS
                username="${credentials[0]}"
                password="${credentials[1]}"
                shift 2
                ;;

            --timeout)
                test -z "$2" && exit_usage
                timeout="$2"
                shift 2
                ;;

            --no-wait)
                wait="false"
                shift 1
                ;;

            --continue-on-failure)
                continue_on_failure="true"
                shift 1
                ;;

            --verbose)
                verbose="true"
                shift 1
                ;;

            --show-serial-log)
                show_serial_log="true"
                shift 1
                ;;

            --no-colors)
                red=""
                green=""
                blue=""
                endc=""
                shift 1
                ;;

            --flash-image-url)
                test -z "$4" && exit_usage
                flash_images+=("{\"name\":\"$2\",\"address\":\"$3\",\"url\":\"$4\"}")
                echo "Flash image \"$2\" added."
                shift 4
                ;;

            --flash-image-file)
                test -z "$4" && exit_usage
                content=$(base64 -w0 <$4)
                flash_images+=("{\"name\":\"$2\",\"address\":\"$3\",\"content\":\"${content}\"}")
                echo "Flash image \"$2\" added."
                shift 4
                ;;

            --flash-image-fill)
                test -z "$5" && exit_usage
                flash_images+=("{\"name\":\"$2\",\"address\":\"$3\",\"fill\":$4,\"size\":$5}")
                echo "Flash image \"$2\" added."
                shift 5
                ;;

            --asset)
                test -z "$3" && exit_usage
                content=$(base64 -w0 <$3)
                assets+=("{\"name\":\"$2\",\"content\":\"${content}\"}")
                echo "Asset \"$2\" added."
                shift 3
                ;;

            --test-case)
                test -z "$2" && exit_usage
                test_cases+=($2)
                echo "Test case \"$(jq -r .name <$2)\" added."
                shift 2
                ;;

            --test-cases)
                test -z "$2" && exit_usage
                shift 1

                while [[ -n "$1" ]] && [[ "$1" != --* ]]; do
                    test_cases+=($1)
                    echo "Test case \"$(jq -r .name <$1)\" added."
                    shift 1
                done
                ;;

            --vars-file)
                test -z "$2" && exit_usage
                vars_file="$2"
                echo "Using vars file \"$2\"."
                shift 2
                ;;

            *)
                echo "Unexpected option: $1."
                exit_usage
                ;;
        esac
    done
}

function check_required_options() {
    if [[ -z "${server_url}" ]]; then
        echo "Option --server-url is required."
        exit_usage
    fi
    if [[ -z "${username}" ]]; then
        echo "One of --credentials and --credentials-file is required."
        exit_usage
    fi
    if [[ -z "${test_cases[0]}" ]]; then
        echo "One of --test-case and --test-cases is required."
        exit_usage
    fi
}

function export_vars() {
    # export some vars so that they are available to test cases
    export SERVER_URL=${server_url}
    export SERVER_URL_HTTP=$(sed -r 's/^https:/http:/g' <<< ${server_url})
    export DOLLAR='$'
}


parse_options "$@"
check_required_commands
check_required_options
export_vars
run_job

exit ${exit_code}

