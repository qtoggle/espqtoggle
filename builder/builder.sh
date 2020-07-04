#!/bin/bash

if [[ -z "${SRC_DIR}" ]]; then
    echo "SRC_DIR not defined"
    exit 1
fi

cd ${SRC_DIR}
RELEASE_DIR=${RELEASE_DIR:-/opt/release}

if [[ -z "${VERSION}" ]]; then
    VERSION=$(/opt/makever.sh)
fi

echo "Using version ${VERSION}"

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

sed -ri "s/0.0.0-unknown.0/${VERSION}/" ${SRC_DIR}/src/ver.h

make clean
make -j4

if [[ -n "${AWS_ACCESS_KEY}" && 
      -n "${AWS_SECRET_KEY}" && 
      -n "${AWS_BUCKET}" && 
      -n "${AWS_FOLDER}" && 
      "${DO_RELEASE}" == "true" ]]; then

    echo "Uploading release to AWS"

    mkdir -p ${RELEASE_DIR}
    cp ${SRC_DIR}/build/user*.bin ${RELEASE_DIR}
    cp ${SRC_DIR}/build/firmware.bin ${RELEASE_DIR}

    if [[ "${VERSION}" =~ beta.[0-9]+$ ]]; then  # beta version
        latest_file="latest_beta"
    elif [[ "${VERSION}" =~ alpha.[0-9]+$ ]]; then  # alpha version
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
