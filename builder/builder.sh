#!/bin/bash

SRC_DIR=${SRC_DIR:-/opt/espqtoggle}
RELEASE_DIR=${RELEASE_DIR:-/opt/release}
export DEBUG=${DEBUG:-false}

# deduce version
if [ -z "${EB_VERSION}" ]; then
    if [ "${EB_CHECKOUT}" == "master" ]; then
        EB_VERSION="latest"
    elif [[ "$EB_CHECKOUT" =~ ^[a-f0-9]{40}$ ]]; then  # special commit id case
        EB_VERSION=git${EB_CHECKOUT::7}
    else
        EB_VERSION=${EB_CHECKOUT}
    fi
fi


set -ae

# setup AWS
S3CFG="/root/.s3cfg"
if ! [ -f ${S3CFG} ]; then
    echo "[default]" > ${S3CFG}
fi
if [ -n "${AWS_ACCESS_KEY}" ]; then
    echo "access_key = ${AWS_ACCESS_KEY}" > ${S3CFG}
fi
if [ -n "${AWS_SECRET_KEY}" ]; then
    echo "secret_key = ${AWS_SECRET_KEY}" > ${S3CFG}
fi

function clone() {
    git clone ${EB_REPO} ${SRC_DIR}
    cd ${SRC_DIR}
    git checkout ${EB_CHECKOUT}
}

function build() {
    cd ${SRC_DIR}
    make -j4
}

function release() {
    CONFIG_NAME=$1
    
    echo "**** preparing configuration ${CONFIG_NAME} ****"
    mkdir -p ${RELEASE_DIR}/${CONFIG_NAME}
    cp ${SRC_DIR}/build/user*.bin ${RELEASE_DIR}/${CONFIG_NAME}
    cp ${SRC_DIR}/build/full.bin ${RELEASE_DIR}/${CONFIG_NAME}
    
    if [[ "${EB_VERSION}" =~ beta.[0-9]+$ ]]; then  # beta version
        latest_file="latest_beta"
    elif [[ "${EB_VERSION}" =~ alpha.[0-9]+$ ]]; then  # alpha version
        latest_file="latest_alpha"
    else  # stable version
        latest_file="latest"
    fi
    
    echo "Version: ${EB_VERSION}" > ${RELEASE_DIR}/${latest_file}
    echo -n "Date: " >> ${RELEASE_DIR}/${latest_file}
    date "+%Y-%m-%d" >> ${RELEASE_DIR}/${latest_file}

    s3cmd put -P --guess-mime-type ${RELEASE_DIR}/${CONFIG_NAME}/user1.bin \
                                   s3://${AWS_BUCKET}/${AWS_FOLDER}/${CONFIG_NAME}/${EB_VERSION}/user1.bin
    s3cmd put -P --guess-mime-type ${RELEASE_DIR}/${CONFIG_NAME}/user2.bin \
                                   s3://${AWS_BUCKET}/${AWS_FOLDER}/${CONFIG_NAME}/${EB_VERSION}/user2.bin
    s3cmd put -P --guess-mime-type ${RELEASE_DIR}/${CONFIG_NAME}/full.bin \
                                   s3://${AWS_BUCKET}/${AWS_FOLDER}/${CONFIG_NAME}/${EB_VERSION}/full.bin
    s3cmd put -P --guess-mime-type ${RELEASE_DIR}/${latest_file} \
                                   s3://${AWS_BUCKET}/${AWS_FOLDER}/${CONFIG_NAME}/${latest_file}
}

function clean() {
    cd ${SRC_DIR}
    make clean
}

if [ -n "${EB_REPO}" ]; then
    clone
    sed -r "s/(FW_VERSION *)\"unknown\"/\1\"${EB_VERSION}\"/" src/ver.h
fi

for conf in ${EB_CONF_FILES}; do
    clean
    echo -e "\n\n**** building configuration ${conf} ****"
    (source ${conf} && build)
    if [[ -n "${AWS_ACCESS_KEY}" && -n "${AWS_SECRET_KEY}" && -n "${AWS_BUCKET}" && -n "${AWS_FOLDER}" && "${DO_RELEASE}" == "true" ]]; then
        release $(cat ${SRC_DIR}/build/.config_name)
    fi
done
