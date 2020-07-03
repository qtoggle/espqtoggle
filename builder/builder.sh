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
if ! [[ -f ${S3CFG} ]]; then
    echo "[default]" > ${S3CFG}
fi
if [[ -n "${AWS_ACCESS_KEY}" ]]; then
    echo "access_key = ${AWS_ACCESS_KEY}" > ${S3CFG}
fi
if [[ -n "${AWS_SECRET_KEY}" ]]; then
    echo "secret_key = ${AWS_SECRET_KEY}" > ${S3CFG}
fi

if [[ -n "${EB_REPO}" ]]; then
    git clone ${EB_REPO} ${SRC_DIR}
    cd ${SRC_DIR}
    git checkout ${EB_CHECKOUT}
else
    cd ${SRC_DIR}
fi

sed -ri "s/(FW_VERSION *)\"0.0.0-unknown.0\"/\1\"${EB_VERSION}\"/" ${SRC_DIR}/src/ver.h

make clean
make -j4

if [[ -n "${AWS_ACCESS_KEY}" && 
      -n "${AWS_SECRET_KEY}" && 
      -n "${AWS_BUCKET}" && 
      -n "${AWS_FOLDER}" && 
      "${DO_RELEASE}" == "true" ]]; then

    echo "uploading release to AWS"

    mkdir -p ${RELEASE_DIR}
    cp ${SRC_DIR}/build/user*.bin ${RELEASE_DIR}
    cp ${SRC_DIR}/build/firmware.bin ${RELEASE_DIR}

    if [[ "${EB_VERSION}" =~ beta.[0-9]+$ ]]; then  # beta version
        latest_file="latest_beta"
    elif [[ "${EB_VERSION}" =~ alpha.[0-9]+$ ]]; then  # alpha version
        latest_file="latest_alpha"
    else  # stable version
        latest_file="latest_stable"
    fi
    
    echo "Version: ${EB_VERSION}" > ${RELEASE_DIR}/${latest_file}
    echo -n "Date: " >> ${RELEASE_DIR}/${latest_file}
    date "+%Y-%m-%d" >> ${RELEASE_DIR}/${latest_file}
    
    cp ${RELEASE_DIR}/${latest_file} ${RELEASE_DIR}/latest

    s3cmd put -P --guess-mime-type ${RELEASE_DIR}/user1.bin \
                                   s3://${AWS_BUCKET}/${AWS_FOLDER}/${EB_VERSION}/user1.bin
    s3cmd put -P --guess-mime-type ${RELEASE_DIR}/user2.bin \
                                   s3://${AWS_BUCKET}/${AWS_FOLDER}/${EB_VERSION}/user2.bin
    s3cmd put -P --guess-mime-type ${RELEASE_DIR}/firmware.bin \
                                   s3://${AWS_BUCKET}/${AWS_FOLDER}/${EB_VERSION}/firmware.bin
    s3cmd put -P --guess-mime-type ${RELEASE_DIR}/${latest_file} \
                                   s3://${AWS_BUCKET}/${AWS_FOLDER}/${latest_file}
    s3cmd put -P --guess-mime-type ${RELEASE_DIR}/latest \
                                   s3://${AWS_BUCKET}/${AWS_FOLDER}/latest
fi
