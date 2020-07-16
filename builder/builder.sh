#!/bin/bash

SRC_DIR=${SRC_DIR:-/src}
RELEASE_DIR=${RELEASE_DIR:-/opt/release}
FLASH_MODES="qio qout dio dout"
FLASH_FREQS="20 26 40 80"

cd ${SRC_DIR}

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

if [[ -n "${AWS_BUCKET}" && -n "${AWS_FOLDER}" && "${AWS_RELEASE}" == "true" ]]; then
    for flash_mode in ${FLASH_MODES}; do
        for flash_freq in ${FLASH_FREQS}; do
            make FLASH_MODE=${flash_mode} FLASH_FREQ=${flash_freq}
            mv build/firmware.bin build/firmware-${flash_mode}-${flash_freq}.bin
        done
    done

    echo "Uploading release to AWS"

    mkdir -p ${RELEASE_DIR}
    cp ${SRC_DIR}/build/user{1,2}.bin ${RELEASE_DIR}
    cp ${SRC_DIR}/build/firmware-*.bin ${RELEASE_DIR}

    if [[ "${VERSION}" =~ beta.[0-9]+$ ]]; then  # beta version
        latest_file="latest_beta"
    elif [[ "${VERSION}" =~ alpha.[0-9]+$ ]]; then  # alpha version
        latest_file="latest_alpha"
    else  # stable version
        latest_file="latest_stable"
    fi
    
    echo "Version: ${VERSION}" > ${RELEASE_DIR}/${latest_file}
    echo -n "Date: " >> ${RELEASE_DIR}/${latest_file}
    date "+%Y-%m-%d" >> ${RELEASE_DIR}/${latest_file}
    
    s3cmd put -P --guess-mime-type ${RELEASE_DIR}/user{1,2}.bin s3://${AWS_BUCKET}/${AWS_FOLDER}/${VERSION}/
    s3cmd put -P --guess-mime-type ${RELEASE_DIR}/firmware-*.bin s3://${AWS_BUCKET}/${AWS_FOLDER}/${VERSION}/
    s3cmd put -P --guess-mime-type ${RELEASE_DIR}/${latest_file} s3://${AWS_BUCKET}/${AWS_FOLDER}/${latest_file}
    s3cmd put -P --guess-mime-type ${RELEASE_DIR}/${latest_file} s3://${AWS_BUCKET}/${AWS_FOLDER}/latest
fi
