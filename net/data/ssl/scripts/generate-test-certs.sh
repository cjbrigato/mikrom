#!/bin/sh

# Copyright 2013 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script generates a set of test (end-entity, intermediate, root)
# certificates that can be used to test fetching of an intermediate via AIA.
set -e -x

# The maximum lifetime for any certificates that may go through a "real"
# cert verifier. This is effectively:
# min(OS verifier max lifetime for local certs, built-in verifier max lifetime
#     for local certs)
#
# The current built-in verifier max lifetime is 39 months
# The current OS verifier max lifetime is 825 days, which comes from
#   iOS 13/macOS 10.15 - https://support.apple.com/en-us/HT210176
# 730 is used here as just a short-hand for 2 years
CERT_LIFETIME=730

rm -rf out
mkdir out
mkdir out/int

openssl rand -hex -out out/2048-sha256-root-serial 16
touch out/2048-sha256-root-index.txt

# Generate the key or copy over the existing one if present.
function copy_or_generate_key {
  existing_pem_filename="$1"
  out_key_filename="$2"
  if grep -q -- '-----BEGIN.*PRIVATE KEY-----' "$existing_pem_filename" ; then
    openssl pkey -in "$existing_pem_filename" -out "$out_key_filename"
  else
    openssl genpkey -algorithm rsa -pkeyopt rsa_keygen_bits:2048 \
      -out "$out_key_filename"
  fi
}

# Generate the key or copy over the existing one if present.
copy_or_generate_key ../certificates/root_ca_cert.pem out/2048-sha256-root.key

# Generate the root certificate
CA_NAME="req_ca_dn" \
  openssl req \
    -new \
    -key out/2048-sha256-root.key \
    -out out/2048-sha256-root.req \
    -config ca.cnf

CA_NAME="req_ca_dn" \
  openssl x509 \
    -req -days 3650 \
    -in out/2048-sha256-root.req \
    -signkey out/2048-sha256-root.key \
    -extfile ca.cnf \
    -extensions ca_cert \
    -text > out/2048-sha256-root.pem

# Generate the test intermediate
openssl rand -hex -out out/int/2048-sha256-int-serial 16
touch out/int/2048-sha256-int-index.txt

# Copy over an existing key if present.
copy_or_generate_key ../certificates/intermediate_ca_cert.pem \
  out/int/2048-sha256-int.key

CA_NAME="req_intermediate_dn" \
  openssl req \
    -new \
    -key out/int/2048-sha256-int.key \
    -out out/int/2048-sha256-int.req \
    -config ca.cnf

CA_NAME="req_intermediate_dn" \
  openssl ca \
    -batch \
    -extensions ca_cert \
    -days 3650 \
    -in out/int/2048-sha256-int.req \
    -out out/int/2048-sha256-int.pem \
    -config ca.cnf

# Generate the leaf certificate requests

copy_or_generate_key ../certificates/expired_cert.pem out/expired_cert.key
openssl req \
  -new \
  -key out/expired_cert.key \
  -out out/expired_cert.req \
  -config ee.cnf

copy_or_generate_key ../certificates/ok_cert.pem out/ok_cert.key
openssl req \
  -new \
  -key out/ok_cert.key \
  -out out/ok_cert.req \
  -config ee.cnf

copy_or_generate_key ../certificates/wildcard.pem out/wildcard.key
openssl req \
  -new \
  -key out/wildcard.key \
  -out out/wildcard.req \
  -reqexts req_wildcard \
  -config ee.cnf

copy_or_generate_key ../certificates/localhost_cert.pem out/localhost_cert.key
SUBJECT_NAME="req_localhost_cn" \
openssl req \
  -new \
  -key out/localhost_cert.key \
  -out out/localhost_cert.req \
  -reqexts req_localhost_san \
  -config ee.cnf

copy_or_generate_key ../certificates/test_names.pem out/test_names.key
openssl req \
  -new \
  -key out/test_names.key \
  -out out/test_names.req \
  -reqexts req_test_names \
  -config ee.cnf

# Generate the leaf certificates
CA_NAME="req_ca_dn" \
  openssl ca \
    -batch \
    -extensions user_cert \
    -startdate 060101000000Z \
    -enddate 070101000000Z \
    -in out/expired_cert.req \
    -out out/expired_cert.pem \
    -config ca.cnf

CA_NAME="req_ca_dn" \
  openssl ca \
    -batch \
    -extensions user_cert \
    -days ${CERT_LIFETIME} \
    -in out/ok_cert.req \
    -out out/ok_cert.pem \
    -config ca.cnf

CA_DIR="out/int" \
CERT_TYPE="int" \
CA_NAME="req_intermediate_dn" \
  openssl ca \
    -batch \
    -extensions user_cert \
    -days ${CERT_LIFETIME} \
    -in out/ok_cert.req \
    -out out/int/ok_cert.pem \
    -config ca.cnf

CA_NAME="req_ca_dn" \
  openssl ca \
    -batch \
    -extensions user_cert \
    -in out/wildcard.req \
    -out out/wildcard.pem \
    -config ca.cnf

CA_NAME="req_ca_dn" \
  openssl ca \
    -batch \
    -extensions user_cert \
    -days ${CERT_LIFETIME} \
    -in out/localhost_cert.req \
    -out out/localhost_cert.pem \
    -config ca.cnf

CA_NAME="req_ca_dn" \
  openssl ca \
    -batch \
    -extensions user_cert \
    -days ${CERT_LIFETIME} \
    -in out/test_names.req \
    -out out/test_names.pem \
    -config ca.cnf

/bin/sh -c "cat out/ok_cert.key out/ok_cert.pem \
    > ../certificates/ok_cert.pem"
/bin/sh -c "cat out/wildcard.key out/wildcard.pem \
    > ../certificates/wildcard.pem"
/bin/sh -c "cat out/localhost_cert.key out/localhost_cert.pem \
    > ../certificates/localhost_cert.pem"
/bin/sh -c "cat out/expired_cert.key out/expired_cert.pem \
    > ../certificates/expired_cert.pem"
/bin/sh -c "cat out/2048-sha256-root.key out/2048-sha256-root.pem \
    > ../certificates/root_ca_cert.pem"
/bin/sh -c "cat out/ok_cert.key out/int/ok_cert.pem \
    out/int/2048-sha256-int.pem \
    > ../certificates/ok_cert_by_intermediate.pem"
/bin/sh -c "cat out/int/2048-sha256-int.key out/int/2048-sha256-int.pem \
    > ../certificates/intermediate_ca_cert.pem"
/bin/sh -c "cat out/int/ok_cert.pem out/int/2048-sha256-int.pem \
    out/2048-sha256-root.pem \
    > ../certificates/x509_verify_results.chain.pem"
/bin/sh -c "cat out/test_names.key out/test_names.pem \
    > ../certificates/test_names.pem"

# Now generate the one-off certs
## Self-signed cert for SPDY/QUIC/HTTP2 pooling testing
openssl req -x509 -days 3650 -extensions req_spdy_pooling \
    -config ../scripts/ee.cnf -newkey rsa:2048 -text \
    -out ../certificates/spdy_pooling.pem

## SubjectAltName parsing
openssl req -x509 -days 3650 -extensions req_san_sanity \
    -config ../scripts/ee.cnf -newkey rsa:2048 -text \
    -out ../certificates/subjectAltName_sanity_check.pem

## SubjectAltName containing www.example.com
openssl req -x509 -days 3650 -extensions req_san_example \
    -config ../scripts/ee.cnf -newkey rsa:2048 -text \
    -out ../certificates/subjectAltName_www_example_com.pem

## certificatePolicies parsing
openssl req -x509 -days 3650 -extensions req_policies_sanity \
    -config ../scripts/ee.cnf -newkey rsa:2048 -text \
    -out ../certificates/policies_sanity_check.pem

## Punycode handling
SUBJECT_NAME="req_punycode_dn" \
  openssl req -x509 -days 3650 -extensions req_punycode \
    -config ../scripts/ee.cnf -newkey rsa:2048 -text \
    -out ../certificates/punycodetest.pem

## SHA1 certificate expiring in 2016.
openssl req -config ../scripts/ee.cnf \
  -newkey rsa:2048 -text -out out/sha1_2016.req
CA_NAME="req_ca_dn" \
  openssl ca \
    -batch \
    -extensions user_cert \
    -startdate 081030000000Z \
    -enddate   161230000000Z \
    -in out/sha1_2016.req \
    -out ../certificates/sha1_2016.pem \
    -config ca.cnf \
    -md sha1

# Includes the canSignHttpExchangesDraft extension
openssl req -x509 -newkey rsa:2048 \
  -keyout out/can_sign_http_exchanges_draft_extension.key \
  -out ../certificates/can_sign_http_exchanges_draft_extension.pem \
  -days 365 \
  -extensions req_extensions_with_can_sign_http_exchanges_draft \
  -nodes -config ee.cnf

# Includes the canSignHttpExchangesDraft extension, but with a SEQUENCE in the
# body rather than a NULL.
openssl req -x509 -newkey rsa:2048 \
  -keyout out/can_sign_http_exchanges_draft_extension_invalid.key \
  -out ../certificates/can_sign_http_exchanges_draft_extension_invalid.pem \
  -days 365 \
  -extensions req_extensions_with_can_sign_http_exchanges_draft_invalid \
  -nodes -config ee.cnf

# SHA-1 certificate issued by locally trusted CA
copy_or_generate_key ../certificates/sha1_leaf.pem out/sha1_leaf.key
openssl req \
  -config ../scripts/ee.cnf \
  -new \
  -text \
  -key out/sha1_leaf.key \
  -out out/sha1_leaf.req
CA_NAME="req_ca_dn" \
  openssl ca \
    -batch \
    -extensions user_cert \
    -days ${CERT_LIFETIME} \
    -in out/sha1_leaf.req \
    -out out/sha1_leaf.pem \
    -config ca.cnf \
    -md sha1
/bin/sh -c "cat out/sha1_leaf.key out/sha1_leaf.pem \
    > ../certificates/sha1_leaf.pem"

# Certificate with only a common name (no SAN) issued by a locally trusted CA
copy_or_generate_key ../certificates/common_name_only.pem \
  out/common_name_only.key
openssl req \
  -config ../scripts/ee.cnf \
  -reqexts req_no_san \
  -new \
  -text \
  -key out/common_name_only.key \
  -out out/common_name_only.req
CA_NAME="req_ca_dn" \
  openssl ca \
    -batch \
    -extensions user_cert \
    -startdate 171220000000Z \
    -enddate   201220000000Z \
    -in out/common_name_only.req \
    -out out/common_name_only.pem \
    -config ca.cnf
/bin/sh -c "cat out/common_name_only.key out/common_name_only.pem \
    > ../certificates/common_name_only.pem"

## Certificates for testing EV display (DN set with different variations)
SUBJECT_NAME="req_ev_dn" \
  openssl req -x509 -days ${CERT_LIFETIME} \
    --config ../scripts/ee.cnf -newkey rsa:2048 -text \
    -out ../certificates/ev_test.pem

SUBJECT_NAME="req_ev_state_only_dn" \
  openssl req -x509 -days ${CERT_LIFETIME} \
    --config ../scripts/ee.cnf -newkey rsa:2048 -text \
    -out ../certificates/ev_test_state_only.pem

# Regenerate CRLSets
## Block a leaf cert directly by SPKI
python3 crlsetutil.py -o ../certificates/crlset_by_leaf_spki.raw \
<<CRLBYLEAFSPKI
{
  "BlockedBySPKI": ["../certificates/ok_cert.pem"]
}
CRLBYLEAFSPKI

## Block a root cert directly by SPKI
python3 crlsetutil.py -o ../certificates/crlset_by_root_spki.raw \
<<CRLBYROOTSPKI
{
  "BlockedBySPKI": ["../certificates/root_ca_cert.pem"]
}
CRLBYROOTSPKI

## Block a leaf cert by issuer-hash-and-serial
python3 crlsetutil.py -o ../certificates/crlset_by_root_serial.raw \
<<CRLBYROOTSERIAL
{
  "BlockedByHash": {
    "../certificates/root_ca_cert.pem": [
      "../certificates/ok_cert.pem"
    ]
  }
}
CRLBYROOTSERIAL

## Block a leaf cert by issuer-hash-and-serial. However, this will be issued
## from an intermediate CA issued underneath a root.
python3 crlsetutil.py -o ../certificates/crlset_by_intermediate_serial.raw \
<<CRLSETBYINTERMEDIATESERIAL
{
  "BlockedByHash": {
    "../certificates/intermediate_ca_cert.pem": [
      "../certificates/ok_cert_by_intermediate.pem"
    ]
  }
}
CRLSETBYINTERMEDIATESERIAL

## Block a subject with a single-entry allowlist of SPKI hashes.
python3 crlsetutil.py -o ../certificates/crlset_by_root_subject.raw \
<<CRLSETBYROOTSUBJECT
{
  "LimitedSubjects": {
    "../certificates/root_ca_cert.pem": [
      "../certificates/root_ca_cert.pem"
    ]
  }
}
CRLSETBYROOTSUBJECT

## Block a subject with an empty allowlist of SPKI hashes.
python3 crlsetutil.py -o ../certificates/crlset_by_root_subject_no_spki.raw \
<<CRLSETBYROOTSUBJECTNOSPKI
{
  "LimitedSubjects": {
    "../certificates/root_ca_cert.pem": []
  },
  "Sequence": 2
}
CRLSETBYROOTSUBJECTNOSPKI

## Block a subject with an empty allowlist of SPKI hashes.
python3 crlsetutil.py -o ../certificates/crlset_by_leaf_subject_no_spki.raw \
<<CRLSETBYLEAFSUBJECTNOSPKI
{
  "LimitedSubjects": {
    "../certificates/ok_cert.pem": []
  }
}
CRLSETBYLEAFSUBJECTNOSPKI

## Mark a given root as blocked for interception.
python3 crlsetutil.py -o \
  ../certificates/crlset_blocked_interception_by_root.raw \
<<CRLSETINTERCEPTIONBYROOT
{
  "BlockedInterceptionSPKIs": [
    "../certificates/root_ca_cert.pem"
  ]
}
CRLSETINTERCEPTIONBYROOT

## Mark a given intermediate as blocked for interception.
python3 crlsetutil.py -o \
  ../certificates/crlset_blocked_interception_by_intermediate.raw \
<<CRLSETINTERCEPTIONBYINTERMEDIATE
{
  "BlockedInterceptionSPKIs": [
    "../certificates/intermediate_ca_cert.pem"
  ]
}
CRLSETINTERCEPTIONBYINTERMEDIATE

## Mark a given root as known for interception, but not blocked.
python3 crlsetutil.py -o \
  ../certificates/crlset_known_interception_by_root.raw \
<<CRLSETINTERCEPTIONBYROOT
{
  "KnownInterceptionSPKIs": [
    "../certificates/root_ca_cert.pem"
  ]
}
CRLSETINTERCEPTIONBYROOT
