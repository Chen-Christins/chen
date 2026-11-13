#!/bin/sh
#
# 重新生成 TLS 测试证书。
# 仅用于 tests/test_tls.cc，禁止用于生产环境。
#
set -eu

cd "$(dirname "$0")"
umask 077

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

CA_DAYS=3650
SERVER_DAYS=3650
SERIAL="${WORK_DIR}/ca.srl"

write_server_ext() {
    local file="$1"
    local subject_alt_name="$2"
    cat >"${file}" <<EOF
basicConstraints=CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=${subject_alt_name}
EOF
}

echo "==> 生成测试 CA"
openssl req -x509 -newkey rsa:2048 -nodes -sha256 \
    -keyout ca.key -out ca.crt -days "${CA_DAYS}" \
    -subj "/CN=Chen Test CA" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign"

echo "==> 生成 server（CN=localhost）"
openssl req -newkey rsa:2048 -nodes -sha256 \
    -keyout server.key -out "${WORK_DIR}/server.csr" \
    -subj "/CN=localhost"
write_server_ext "${WORK_DIR}/server.ext" "DNS:localhost,IP:127.0.0.1"
openssl x509 -req -in "${WORK_DIR}/server.csr" -sha256 \
    -CA ca.crt -CAkey ca.key -CAcreateserial -CAserial "${SERIAL}" \
    -out server.crt -days "${SERVER_DAYS}" \
    -extfile "${WORK_DIR}/server.ext"

echo "==> 生成 wrong-host（SAN=wrong.example.com）"
openssl req -newkey rsa:2048 -nodes -sha256 \
    -keyout wrong-host.key -out "${WORK_DIR}/wrong-host.csr" \
    -subj "/CN=wrong.example.com"
write_server_ext "${WORK_DIR}/wrong-host.ext" "DNS:wrong.example.com"
openssl x509 -req -in "${WORK_DIR}/wrong-host.csr" -sha256 \
    -CA ca.crt -CAkey ca.key -CAserial "${SERIAL}" \
    -out wrong-host.crt -days "${SERVER_DAYS}" \
    -extfile "${WORK_DIR}/wrong-host.ext"

echo "==> 生成 unknown-ca（自签名）"
openssl req -x509 -newkey rsa:2048 -nodes -sha256 \
    -keyout unknown-ca.key -out unknown-ca.crt -days "${SERVER_DAYS}" \
    -subj "/CN=localhost" \
    -addext "basicConstraints=critical,CA:FALSE" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"

echo "==> 生成 expired（2019-2020，CA 签发）"
openssl req -newkey rsa:2048 -nodes -sha256 \
    -keyout expired.key -out "${WORK_DIR}/expired.csr" \
    -subj "/CN=localhost"

mkdir -p "${WORK_DIR}/newcerts"
: >"${WORK_DIR}/index.txt"
echo 1000 >"${WORK_DIR}/serial"
cat >"${WORK_DIR}/ca.cnf" <<EOF
[ ca ]
default_ca = CA_default

[ CA_default ]
dir = ${WORK_DIR}
database = \$dir/index.txt
new_certs_dir = \$dir/newcerts
certificate = ${PWD}/ca.crt
private_key = ${PWD}/ca.key
serial = \$dir/serial
default_md = sha256
default_days = ${SERVER_DAYS}
policy = policy_any
unique_subject = no
x509_extensions = expired_ext

[ policy_any ]
commonName = supplied

[ expired_ext ]
basicConstraints = CA:FALSE
keyUsage = critical,digitalSignature,keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = DNS:localhost,IP:127.0.0.1
EOF

# OpenSSL 3.0 的 x509 子命令不支持 -not_before/-not_after，用 ca 子命令指定固定起止时间
openssl ca -batch -config "${WORK_DIR}/ca.cnf" \
    -in "${WORK_DIR}/expired.csr" -out expired.crt -notext \
    -startdate 20190101000000Z -enddate 20200101000000Z

echo "==> 完成"
openssl verify -CAfile ca.crt server.crt
openssl verify -CAfile ca.crt wrong-host.crt
